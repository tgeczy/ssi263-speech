// FirmwareImport's layout and wording on the Mac, with no device and no firmware: Android's FirmwareImportTest.kt in
// Swift, every case of it, plus the Accents' files and the zip forms the Apple side reads itself (ZipReader.swift:
// stored and deflated entries, sizes in a data descriptor).  The native side's judgement is played by a fake that
// knows a made-up image by the same signature (F3 C3 xx xx FF "COPYRIGHT") plus a letter for what it is, a made-up
// Intel HEX text as the Speak-Out's when it says GOOD, and made-up Aicom files by a first line ("AICOM u2" ...).  The
// real judgement has its own tests on the real files (test_apple_speech.py, test_android_native.py).
//
//     sh src/platforms/apple/test/run_swift_tests.sh                      every case
//     SSI263_IMPORT_BREAK=1|unopened|state|speakout|accent|release sh .../run_swift_tests.sh   the controls: each must FAIL

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
/// S (Spanish), U (a Braille Lite 2000 release not on the list), H (Hungarian: a release voice), J (Japanese: a
/// release no voice speaks, the unit tab's alone) or T (Type 'n Speak).
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

/// A made-up early Mockingboard file, and a made-up Mockingboard disk 1 (the fake gives the early file from it).
let earlyFile = bytes("SWEETEARLY") + filled(1900) { UInt8(($0 * 3) & 0xFF) }
let earlyLabel = "Mockingboard, early: Sweet Micro Systems' text-to-speech of Mockingboard disk 1"
func disk1() -> Data { bytes("DOS33DISK") + bytes("DISK1") + Data(count: 3000) }

/// A made-up disk image: the toolkit's when it says TOOLKIT (the fake then gives the file), another when not.
func disk(_ toolkit: Bool) -> Data { bytes("DOS33DISK") + bytes(toolkit ? "TOOLKIT" : "OTHER") + Data(count: 3000) }

/// Made-up Sounding Board drivers: the native side knows the real ones by their sha256s.
let sbLoad = bytes("SBLOAD") + filled(1300) { UInt8(($0 * 7) & 0xFF) }
let sbCom = bytes("SBCOM") + filled(740) { UInt8(($0 * 9) & 0xFF) }

/// A made-up S_TALK.EXE: the native side knows the real one by its sha256.
let sTalk = bytes("SMARTALKEX") + filled(2500) { UInt8(($0 * 13) & 0xFF) }
let sTalkLabel = "SmarTalk: Automated Functions' S_TALK.EXE 1.21"

/// A made-up state: the size every state has, whatever is in it.
func state() -> Data { filled(FirmwareImport.stateSize) { UInt8(($0 * 13) & 0xFF) } }

struct Fake: FirmwareImport.Identify {
    static let labels = ["English June 2003", "English September 2000", "Spanish September 2000",
                         "German September 2000", "Italian September 2000", "Slovak Braille 'n Speak 2000"]

    func firmware(_ data: Data, out: URL) -> (Int, String) {
        let text = String(data: data.prefix(16), encoding: .isoLatin1) ?? ""
        if text.hasPrefix("DOS33DISK") {
            if (String(data: data, encoding: .isoLatin1) ?? "").contains("DISK1") {
                try? earlyFile.write(to: out)
                return (FirmwareImport.mockingboardEarly, earlyLabel)
            }
            if !(String(data: data, encoding: .isoLatin1) ?? "").contains("TOOLKIT") {
                return (FirmwareImport.otherDisk, "a Mockingboard disk, but not the text-to-speech version 1.1 this " +
                        "voice runs")
            }
            try? mockingboardFile.write(to: out)
            return (FirmwareImport.mockingboard, "Mockingboard: Sweet Micro Systems' text-to-speech 1.1")
        }
        if text.hasPrefix("SWEETEARLY") {
            try? data.write(to: out)
            return (FirmwareImport.mockingboardEarly, earlyLabel)
        }
        if text.hasPrefix("SWEETMICRO") {
            try? data.write(to: out)
            return (FirmwareImport.mockingboard, "Mockingboard: Sweet Micro Systems' text-to-speech 1.1")
        }
        if text.hasPrefix("SBLOAD") || text.hasPrefix("SBCOM") {
            try? data.write(to: out)
            return text.hasPrefix("SBLOAD") ? (FirmwareImport.sbLoad, "GW Micro Sounding Board 2.7: SBLOAD.COM")
                : (FirmwareImport.sbCom, "GW Micro Sounding Board 2.7: SB.COM")
        }
        if text.hasPrefix("SMARTALKEX") {
            try? data.write(to: out)
            return (FirmwareImport.smartalk, sTalkLabel)
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
        case "G": (kind, label) = (FirmwareImport.german, Fake.labels[3])
        case "I": (kind, label) = (FirmwareImport.italian, Fake.labels[4])
        case "K": (kind, label) = (FirmwareImport.slovak, Fake.labels[5])
        case "H": (kind, label) = (FirmwareImport.release + Fake.hungarian, "Braille Lite Hungarian")
        case "J": (kind, label) = (FirmwareImport.release + Fake.japanese, "Braille Lite Japanese")
        case "U": return (FirmwareImport.unknown, "not a release this app knows")
        default: return (FirmwareImport.refused, "not a release this voice can run")
        }
        try? Data(b[at...]).write(to: out)
        return (kind, label)
    }

    func known() -> [String] { Fake.labels }

    /// Two rows of a made-up release table: a release voice (its voice number made up too), and one no voice speaks.
    static let hungarian = 7, japanese = 20
    func release(_ row: Int) -> FirmwareImport.Release? {
        switch row {
        case Fake.hungarian:
            return FirmwareImport.Release(id: "bl2_hu_1999_07", name: "Braille Lite 2000 (Hungarian)",
                                          file: "BL2HUN.BNS", voice: 21)
        case Fake.japanese:
            return FirmwareImport.Release(id: "bl2_ja", name: "Braille Lite 2000 (Japanese)", file: "BL2JPN.BNS",
                                          voice: -1)
        default: return nil
        }
    }

    func smartalkSha256() -> String { Fake.smartalkSha256 }
    static let smartalkSha256 = String(repeating: "5a", count: 32)
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

test("firmwareInAnyFolderIsFound") {
    // judged by its content, never by its folder (Tomi, 2026-10-18: anyone's own archive imports)
    let plan = inspect("deep.zip", zip([("backup/2001/blt2000/disk1/BL2ENG.BNS", bns("E"))]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [E])
    eq(plan.found.first?.from, "backup/2001/blt2000/disk1/BL2ENG.BNS")
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
        "September 2000; German September 2000; Italian September 2000; Slovak Braille 'n Speak 2000.") == true,
          plan.refusal ?? "nil")
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

// A state is larger once the unit's files reach past the RAM's first 256 KB or the flash's first 512 KB (bl_board.h
// bl_save_state), and the emulator's ends with its clock's 64 bytes: each is still a state.  The Spanish Braille
// Lite's own factory state is one of them (its setup fills all the RAM).

func stateTail(_ mark: [UInt8]) -> Data { Data(mark) + Data(repeating: 7, count: 64 - mark.count) }
let clockTail = stateTail(Array("BLCLOCK1".utf8))
let chipTail = stateTail(Array("BLR6242".utf8) + [1])           // the 640's clock chip

test("everySizeAStateCanHaveIsAState") {
    eq(FirmwareImport.stateSizes, Set([786432, 1310720, 2359296, 2883584]))
    for size in FirmwareImport.stateSizes.sorted() {
        let s = Data(repeating: 13, count: size)
        eq(inspect("backup.bin", s).refusal, FirmwareImport.stateFile)
        eq(inspect("backup.bin", s + clockTail).refusal, FirmwareImport.stateFile)
        eq(inspect("backup.bin", s + chipTail).refusal, FirmwareImport.stateFile)
    }
}

test("aLargerStateBesideFirmwareIsNotUsed") {
    let spanish = Data(repeating: 13, count: 1310720)
    let plan = inspect("unit.zip", zip([("BL2ENG.BNS", bns("E")), ("bl2spa_fresh.state", spanish),
                                        ("saved.dat", spanish + clockTail)]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [E])
    eq(plan.notes.count, 1)
    let note = plan.notes.first ?? ""
    check(note.contains("bl2spa_fresh.state") && note.contains("saved.dat") && note.contains("state file"), note)
}

test("aStatesSizeWithOtherBytesAfterItIsNotAState") {
    let s = Data(repeating: 13, count: FirmwareImport.stateSize)
    check(!FirmwareImport.isState(s + Data(repeating: 7, count: 64)), "64 more bytes that are no clock's")
    check(!FirmwareImport.isState(s + Data(count: 1)), "one more byte")
    check(!FirmwareImport.isState(Data(count: 262144 + 2097152 + 1)), "a byte past the 2 MB flash")
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

test("aHexTwoFoldersDownIsFound") {
    eq(kinds(inspect("deep.zip", zip([("backup/speakout/SPEAKOUT.HEX", hex(true))]))), [SO])
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

test("theMockingboardFileTwoFoldersDownIsFound") {
    eq(kinds(inspect("deep.zip", zip([("backup/mockingboard/mockingboard-tts-1.1.bin", mockingboardFile)]))), [MB])
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

test("aDiskTwoFoldersDownIsFound") {
    eq(kinds(inspect("deep.zip", zip([("backup/apple/toolkit.po", disk(true))]))), [MB])
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

// ---- the early Mockingboard: disk 1's text-to-speech, its file or the disk (Tomi, 2026-10-10) ---------------------

let MBE = FirmwareImport.mockingboardEarly

test("theEarlyFileAlone") {
    let plan = inspect("mockingboard-tts-early.bin", earlyFile)
    eq(plan.refusal, nil)
    eq(kinds(plan), [MBE])
    eq(plan.found.first?.label, earlyLabel)
}

test("diskOneInAFolderOfAZipGivesTheEarlyFile") {
    let plan = inspect("apple.zip", zip([("disks/mockingboard1.dsk", disk1())]))
    eq(kinds(plan), [MBE])
    eq(plan.found.first?.from, "disks/mockingboard1.dsk")
    eq(plan.found.first.flatMap { try? Data(contentsOf: $0.firmware) }, earlyFile)
}

test("bothMockingboardsFromOneZip") {
    let plan = inspect("mockingboard.zip", zip([("toolkit.dsk", disk(true)), ("disk1/mockingboard1.dsk", disk1())]))
    eq(kinds(plan), [MB, MBE])
    eq(FirmwareImport.files[MBE], "mockingboard-tts-early.bin")
    eq(FirmwareImport.stateFiles[MBE], nil)
}

// ---- 0.8's Blazie units: German, Italian (both Lite2000.BNS on their disks), the Slovak Braille 'n Speak ---------

let DE = FirmwareImport.german, IT = FirmwareImport.italian, SK = FirmwareImport.slovak

test("theGermanAndItalianDisksOneFolderDownBesideTheSlovakUnit") {
    let plan = inspect("blazie.zip", zip([("german/Lite2000.BNS", bns("G")), ("italian/Lite2000.BNS", bns("I")),
                                          ("bns2000/BS2SLL.BNS", bns("K"))]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [DE, IT, SK])
    eq(plan.found.map { $0.from }, ["german/Lite2000.BNS", "italian/Lite2000.BNS", "bns2000/BS2SLL.BNS"])
    eq(plan.found.count > 2 ? plan.found[2].label : "", "Slovak Braille 'n Speak 2000")
}

test("theUnifiedAddOnGivesEveryUnitButNoState") {
    let u = "synthDrivers/_ssi263_unified/"
    let d = u + "_ssi263_blazie/"
    let plan = inspect("ssi263-speech-0.8.0.nvda-addon", zip([("manifest.ini", bytes("name = ssi263")),
        (u + "__init__.py", bytes("#")), (d + "BL2ENG.BNS", bns("E")), (d + "bl2_2003_warm.state", state()),
        (d + "BL2SPA.BNS", bns("S")), (d + "Lite2000_de.BNS", bns("G")), (d + "Lite2000_it.BNS", bns("I")),
        (d + "BS2SLL.BNS", bns("K")), (d + "bs2sll_fresh.state", state()),
        (u + "_ssi263_speakout/SPEAKOUT.HEX", hex(true)),
        (u + "_ssi263_mockingboard/mockingboard-tts-1.1.bin", mockingboardFile),
        (u + "_ssi263_mockingboard/mockingboard-tts-early.bin", earlyFile),
        (u + "_ssi263_accent/accent-sa/u2.BIN", aicom("u2")), (u + "_ssi263_accent/accent-sa/u3.BIN", aicom("u3")),
        (u + "_ssi263_accent/accent-sa/u4.BIN", aicom("u4")), (u + "_ssi263_accent/SPKEMS.DVC", aicom("dvc"))]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [E, S, DE, IT, SK, SO, MB, MBE] + SA + [FirmwareImport.accentMini])
    eq(plan.notes.count, 1)
    check(plan.notes.first?.hasPrefix("\(d)bl2_2003_warm.state, \(d)bs2sll_fresh.state are state files, not " +
        "firmware, and are not used") == true, "\(plan.notes)")
}

test("theNvdaAddOnsFlatNamesGiveEveryBlazieUnit") {
    let d = "synthDrivers/_ssi263_blazie/"
    let plan = inspect("blazie.nvda-addon", zip([(d + "BL2ENG.BNS", bns("E")), (d + "BL2SPA.BNS", bns("S")),
        (d + "Lite2000_de.BNS", bns("G")), (d + "lite2000_de_fresh.state", state()),
        (d + "Lite2000_it.BNS", bns("I")), (d + "BS2SLL.BNS", bns("K"))]))
    eq(kinds(plan), [E, S, DE, IT, SK])
}

test("eachBlazieUnitHasFilesOfItsOwnAndAState") {
    let names = Array(FirmwareImport.files.values) + Array(FirmwareImport.stateFiles.values)
    eq(names.count, Set(names).count)
    eq(FirmwareImport.files[DE], "Lite2000_de.BNS")
    eq(FirmwareImport.stateFiles[DE], "lite2000_de_fresh.state")
    eq(FirmwareImport.files[IT], "Lite2000_it.BNS")
    eq(FirmwareImport.files[SK], "BS2SLL.BNS")
    eq(FirmwareImport.stateFiles[SK], "bs2sll_fresh.state")
    for v in FirmwareImport.blazie { check(FirmwareImport.stateFiles[v] != nil, "no state for \(v)") }
    eq(Set(FirmwareImport.order), Set(FirmwareImport.files.keys))
    eq(FirmwareImport.name(of: DE), "German Braille Lite")
    eq(FirmwareImport.name(of: IT), "Italian Braille Lite")
    eq(FirmwareImport.name(of: SK), "Slovak Braille 'n Speak 2000")
    eq(FirmwareImport.name(of: SO), "Speak-Out")
}

// ---- the release table's other releases: a release voice's (Hungarian ...), or the unit tab's alone -------------

let HU = FirmwareImport.release + Fake.hungarian, JA = FirmwareImport.release + Fake.japanese

test("aReleaseVoiceIsTakenWithItsOwnFiles") {
    let plan = inspect("BL2HUN.BNS", bns("H"))
    eq(plan.refusal, nil)
    eq(kinds(plan), [HU])
    let kept = FirmwareImport.keptAs(HU, Fake())
    eq(kept?.file, "BL2HUN.BNS")
    eq(kept?.state, "bl2_hu_1999_07.state")
    eq(FirmwareImport.name(of: HU, Fake()), "Braille Lite 2000 (Hungarian)")
}

test("aReleaseNoVoiceSpeaksIsKeptForTheUnitTab") {
    let plan = inspect("BL2JPN.BNS", bns("J"))
    eq(plan.refusal, nil)
    eq(kinds(plan), [JA])
    let kept = FirmwareImport.keptAs(JA, Fake())
    eq(kept?.file, "units/bl2_ja.BNS")
    eq(kept?.state, nil)
}

test("releasesComeAfterTheShippedUnitsInOneZip") {
    let plan = inspect("bl2000.zip", zip([("BL2JPN.BNS", bns("J")), ("BL2HUN.BNS", bns("H")),
                                          ("BL2ENG.BNS", bns("E"))]))
    eq(kinds(plan), [E, HU, JA])
    eq(plan.notes, [])
}

test("theAddOnsLanguageUpdatesComeInTheSameImport") {
    // the unified add-on keeps the Blazie engine's language updates one folder further, bl2000/, with their states
    let d = "synthDrivers/_ssi263_unified/_ssi263_blazie/"
    let plan = inspect("ssi263-speech-0.8.0.nvda-addon", zip([("manifest.ini", bytes("name = ssi263")),
        (d + "BL2ENG.BNS", bns("E")), (d + "bl2_2003_warm.state", state()),
        (d + "bl2000/BL2HUN.BNS", bns("H")), (d + "bl2000/bl2_hu_1999_07.state", state()),
        (d + "bl2000/BL2JPN.BNS", bns("J")), (d + "bl2000/bl2_x1.state", state()),
        (d + "bl2000/bl2_x2.state", state()), (d + "bl2000/bl2_x3.state", state())]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [E, HU, JA])
    eq(plan.found.dropFirst().first?.from, d + "bl2000/BL2HUN.BNS")
    eq(plan.notes, ["5 state files (\(d)bl2_2003_warm.state and others) are not firmware, and are not used: this " +
        "device prepares each unit's state itself from its firmware."])
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

test("aRomTwoFoldersDownOutsideAnAddOnIsFound") {
    eq(kinds(inspect("deep.zip", zip([("backup/accent/u2.BIN", aicom("u2"))]))), [FirmwareImport.accentU2])
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
    eq(FirmwareImport.noFirmwareFile, "This file does not contain Braille Lite, Braille 'n Speak, Speak-Out, " +
        "Sounding Board, SmarTalk, Mockingboard or Accent firmware.")
    check(FirmwareImport.whatToChoose.contains("Lite2000.BNS (German, Italian)"), "")
    check(FirmwareImport.whatToChoose.contains("For the Slovak Braille 'n Speak 2000: BS2SLL.BNS"), "")
    check(FirmwareImport.whatToChoose.contains("your own copy of Mockingboard disk 1"), "")
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

// ---- the Sounding Board: two files, imported only together (0.8.5) ------------------------------------------------

let SB = FirmwareImport.soundingboard

test("sbZipAsGwMicroGaveItOutIsOneVoiceOfTwoFiles") {
    let plan = inspect("sb.zip", zip([("SBLOAD.COM", sbLoad), ("SB.COM", sbCom), ("SECT1.TXT", bytes("manual")),
                                      ("SOUNDBRD.DLL", bytes("MZ viewer")), ("SB.INF", bytes("[sb]"))]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [SB])
    eq(plan.found.first?.label, "GW Micro Sounding Board 2.7: SBLOAD.COM and SB.COM")
    eq(plan.found.first?.from, "SBLOAD.COM and SB.COM")
    eq(plan.found.first.flatMap { try? Data(contentsOf: $0.firmware) }, sbLoad)
    eq(plan.found.first?.parts.count, 1)
    eq(plan.found.first?.parts.first.flatMap { try? Data(contentsOf: $0) }, sbCom)
    eq(plan.notes, [])
}

test("oneSoundingBoardDriverAloneIsRefusedWithWhy") {
    let plan = inspect("SB.COM", sbCom)
    eq(plan.found.count, 0)
    check(plan.refusal?.hasPrefix("SB.COM is the Sounding Board's SB.COM, but its SBLOAD.COM is needed too (both " +
                                  "are in GW Micro's sb.zip).") == true, plan.refusal ?? "nil")
    let other = inspect("half.zip", zip([("BL2ENG.BNS", bns("E")), ("SBLOAD.COM", sbLoad)]))
    eq(kinds(other), [E])
    check(other.notes.contains("SBLOAD.COM is the Sounding Board's SBLOAD.COM, but its SB.COM is needed too (both " +
                               "are in GW Micro's sb.zip); it is left out."), "\(other.notes)")
}

test("the085AddOnBringsTheSoundingBoardItCarries") {
    // 0.8.5's add-on carries GW Micro's two drivers beside SPEAKOUT.HEX (Tomi, 2026-10-18): one import, both voices
    let s = "synthDrivers/_ssi263_unified/_ssi263_speakout/"
    let plan = inspect("ssi263-speech-0.8.5.nvda-addon", zip([("manifest.ini", bytes("name = ssi263")),
        (s + "SPEAKOUT.HEX", hex(true)), (s + "SBLOAD.COM", sbLoad), (s + "SB.COM", sbCom)]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [SO, SB])
    eq(plan.found.last?.parts.first.flatMap { try? Data(contentsOf: $0) }, sbCom)
}

test("theTwoDriversChosenAsTwoFilesAreOneVoice") {
    tmpCount += 1
    var ignored: [String] = []
    let plan = FirmwareImport.inspect(files: [("SB.COM", sbCom), ("README.TXT", bytes("hello")),
                                              ("SBLOAD.COM", sbLoad)],
                                      staging: tmpRoot.appendingPathComponent("s\(tmpCount)"), id: Fake(),
                                      ignored: &ignored)
    eq(plan.refusal, nil)
    eq(kinds(plan), [SB])
    eq(plan.found.first.flatMap { try? Data(contentsOf: $0.firmware) }, sbLoad)
    eq(plan.found.first?.parts.first.flatMap { try? Data(contentsOf: $0) }, sbCom)
    eq(ignored, ["README.TXT"])
}

test("theSoundingBoardIsNamedForTheFilesToBring") {
    check(FirmwareImport.whatToChoose.contains("For the Sounding Board: GW Micro's sb.zip, or the SBLOAD.COM and " +
                                               "SB.COM inside it (both are needed)."), "")
    check(FirmwareImport.order.contains(SB), "the order has no Sounding Board")
    eq(FirmwareImport.partFiles[SB], ["SBLOAD.COM", "SB.COM"])
    eq(FirmwareImport.files[SB], "SBLOAD.COM")
}

// ---- Automated Functions' SmarTalk: S_TALK.EXE, one file, from its disk or the NVDA add-on that carries it (0.9.0) -

let ST = FirmwareImport.smartalk

test("theSmarTalksExeIsItsVoice") {
    let plan = inspect("S_TALK.EXE", sTalk)
    eq(plan.refusal, nil)
    eq(kinds(plan), [ST])
    eq(plan.found.first?.label, sTalkLabel)
    eq(plan.found.first.flatMap { try? Data(contentsOf: $0.firmware) }, sTalk)
    eq(plan.found.first?.parts.count, 0)
    eq(FirmwareImport.keptAs(ST, Fake())?.file, "S_TALK.EXE")
    check(FirmwareImport.keptAs(ST, Fake())?.state == nil, "the SmarTalk has a state")
}

test("theAddOnBringsTheSmarTalkItCarries") {
    // the add-on carries S_TALK.EXE beside the SmarTalk's driver (nvda/build_ssi263.py stage_smartalk), with its
    // notice: one import brings it with the Speak-Out
    let u = "synthDrivers/_ssi263_unified/"
    let plan = inspect("ssi263-speech.nvda-addon", zip([("manifest.ini", bytes("name = ssi263")),
        (u + "_ssi263_speakout/SPEAKOUT.HEX", hex(true)), (u + "_ssi263_smartalk/S_TALK.EXE", sTalk),
        (u + "_ssi263_smartalk/licenses/SmarTalk-software-notice.txt", bytes("Copyright 1995"))]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [SO, ST])
    eq(plan.found.last.flatMap { try? Data(contentsOf: $0.firmware) }, sTalk)
}

test("theSmarTalksDiskAsAZipIsItsVoice") {
    // the disk's own files, as its zip holds them: the program beside its tools, manuals and a folder
    let plan = inspect("SmarTalk V1.21 disk.zip", zip([("INSTALL.BAT", bytes("@echo off")),
        ("README.TXT", bytes("SmarTalk")), ("S_TALK.EXE", sTalk), ("XS_TALK.EXE", bytes("MZ another")),
        ("SETSMART.EXE", bytes("MZ setup")), ("SMARTALK/SMARTALK.DLL", bytes("MZ dll"))]))
    eq(plan.refusal, nil)
    eq(kinds(plan), [ST])
    eq(plan.found.first?.from, "S_TALK.EXE")
}

test("anotherSTalkExeIsToldWhichCopyTheVoiceRuns") {
    // another version, or a copy changed: the words name the one it runs, by its size and sha256 (said only here)
    let which = "not the SmarTalk's S_TALK.EXE 1.21, the copy this voice runs (36,882 bytes, SHA-256 " +
        Fake.smartalkSha256 + ")"
    let plan = inspect("S_TALK.EXE", bytes("MZ version 1.10"))
    eq(plan.found.count, 0)
    check(plan.refusal?.hasPrefix("S_TALK.EXE is " + which + ". Choose firmware files you own.") == true,
          plan.refusal ?? "nil")
    // its name in any case, in a zip's folder, beside a voice that is taken: a note
    let other = inspect("mine.zip", zip([("SPEAKOUT.HEX", hex(true)), ("old/s_talk.exe", bytes("MZ 1.10"))]))
    eq(kinds(other), [SO])
    eq(other.notes, ["old/s_talk.exe is " + which + "; it is left out."])
    // a zip in it, too
    let inner = inspect("outer.zip", zip([("SPEAKOUT.HEX", hex(true)),
                                          ("disk.zip", zip([("S_TALK.EXE", bytes("MZ 1.10"))]))]))
    eq(inner.notes, ["disk.zip, S_TALK.EXE is " + which + "; it is left out."])
    // the name alone words it: the same bytes under another name are no firmware, and say nothing of the SmarTalk
    let renamed = inspect("PROGRAM.EXE", bytes("MZ version 1.10"))
    check(renamed.refusal?.hasPrefix(FirmwareImport.noFirmwareFile) == true, renamed.refusal ?? "nil")
    // the real one is the voice, whatever it is called
    eq(kinds(inspect("SMARTALK.BIN", sTalk)), [ST])
}

test("theSmarTalkIsNamedForTheFileToBring") {
    check(FirmwareImport.whatToChoose.contains("For the SmarTalk: Automated Functions' S_TALK.EXE 1.21 (36,882 " +
                                               "bytes), from the SmarTalk V1.21 disk, or a zip holding it."), "")
    eq(FirmwareImport.order.filter { $0 == SB || $0 == ST }, [SB, ST])
    eq(FirmwareImport.files[ST], "S_TALK.EXE")
    eq(FirmwareImport.name(of: ST), "SmarTalk")
}

// ---- any folder of a zip, and a zip in it (Android's 0.8.5 import, 2026-10-18) ------------------------------------

let DE2 = FirmwareImport.german, SK2 = FirmwareImport.slovak

test("theEmulatorDownloadBringsEveryUnitItCarries") {
    // blazie-emu-0.8.0-windows.zip as it is: the program beside firmware/, each unit's files in it or below it (the
    // 640 three folders down; the fake's unit-tab-only release, J, plays it)
    let plan = inspect("blazie-emu-0.8.0-windows.zip", zip([("blazie_emu.exe", bytes("MZ") + Data(count: 900)),
        ("firmware/BL2ENG.BNS", bns("E")), ("firmware/bl2_2003_warm.state", state()),
        ("firmware/spanish/BL2SPA.BNS", bns("S")), ("firmware/german/Lite2000.BNS", bns("G")),
        ("firmware/bl2000/BL2HUN.BNS", bns("H")), ("firmware/bns2000/BS2SLL.BNS", bns("K")),
        ("firmware/bns640/june2003/BSPENG.BNS", bns("J"))]))
    eq(plan.refusal, nil)
    eq(Set(kinds(plan)), Set([E, S, DE2, SK2, HU, JA]))
    // the same folder one level down (a zip someone made of the download's folder), and deeper still
    eq(kinds(inspect("emu.zip", zip([("blazie-emu-0.8.0-windows/firmware/spanish/BL2SPA.BNS", bns("S"))]))), [S])
    eq(kinds(inspect("deep.zip", zip([("firmware/a/b/c/d/BL2SPA.BNS", bns("S"))]))), [S])
}

test("theFirmwareCollectionBringsEveryUnitAndKnowsItsArchive") {
    // blazie-firmware-collection-<date>.zip: the emulator's folder under another name, and system/ with every release
    // of every unit -- the same release twice is one, another release is said, a unit no app runs and a program are
    // not firmware to take
    let plan = inspect("blazie-firmware-collection-2026-10-18.zip", zip([("README.txt", bytes("text")),
        ("emulator-firmware-folder/BL2ENG.BNS", bns("E")), ("emulator-firmware-folder/bl2_2003_warm.state", state()),
        ("emulator-firmware-folder/spanish/BL2SPA.BNS", bns("S")),
        ("emulator-firmware-folder/bl2000/BL2HUN.BNS", bns("H")),
        ("emulator-firmware-folder/bns640/june2003/BSPENG.BNS", bns("J")),
        ("system/BL2 Braille Lite 2000/BL2ENG_a55455eb.BNS", bns("E")),
        ("system/BL2 Braille Lite 2000/BL2ENG_a4d13165.BNS", bns("e")),
        ("system/BL4 Braille Lite 40/BL4ENG-1.BNS", bns("T")),
        ("system/BSP Braille n Speak 640/BSPENG_ff0738fe.BNS", bns("J")),
        ("programs/ADDCAL.BNS", Data(repeating: 0x18, count: 4000))]))
    eq(plan.refusal, nil)
    eq(Set(kinds(plan)), Set([E, S, HU, JA]))
    eq(plan.found.count, 4)
    eq(plan.found.first { $0.kind == E }?.from, "emulator-firmware-folder/BL2ENG.BNS")
    check(plan.notes.contains { $0.hasPrefix("system/BL2 Braille Lite 2000/BL2ENG_a4d13165.BNS is another English " +
                                             "release") }, "\(plan.notes)")
    check(!plan.notes.contains { $0.contains("a55455eb") }, "\(plan.notes)")
}

test("anArchiveOfOnesOwnFoldersBringsEverySynth") {
    // Tomi, 2026-10-18: "top-level folders for each synth name: one gwmicro/sb.zip, gwmicro/speakout.hex and so on"
    // -- any folders, any names, zips in it opened; the add-on in it too, bigger than any firmware
    var g = SystemRandomNumberGenerator()
    let noise = Data((0..<(5 << 20)).map { _ in UInt8.random(in: 0...255, using: &g) })
    let plan = inspect("my-synths.zip", zip([
        ("synths/gwmicro/sb.zip", zip([("SBLOAD.COM", sbLoad), ("SB.COM", sbCom), ("README.DOC", bytes("x"))])),
        ("synths/gwmicro/speakout.hex", hex(true)),
        ("synths/blazie/braille lite 2000/english/BL2ENG.BNS", bns("E")),
        ("synths/sweet micro/disks/toolkit.dsk", disk(true)),
        ("synths/aicom/roms/u3.BIN", aicom("u3")),
        ("add-ons/ssi263-speech-0.8.5.nvda-addon", zip([
            ("synthDrivers/_ssi263_unified/_ssi263_blazie/bl2000/BL2HUN.BNS", bns("H")), ("noise.bin", noise)],
            style: .stored)),
        ("photos/cat.jpg", noise + noise.prefix(1 << 20))], style: .stored))
    eq(plan.refusal, nil)
    eq(Set(kinds(plan)), Set([E, SO, MB, SB, FirmwareImport.accentU3, HU]))
    eq(plan.found.first { $0.kind == SB }?.from, "synths/gwmicro/sb.zip, SBLOAD.COM and synths/gwmicro/sb.zip, SB.COM")
    eq(plan.found.first { $0.kind == HU }?.from, "add-ons/ssi263-speech-0.8.5.nvda-addon, synthDrivers/" +
        "_ssi263_unified/_ssi263_blazie/bl2000/BL2HUN.BNS")
    eq(plan.notes, [])
}

test("aUnitImportedAloneIsSaidToBeNoVoice") {
    // Tomi, 2026-10-18: a Braille 'n Speak 640 alone is "not a voice", never "the voice speaks now"
    eq(FirmwareImport.doneMessage(inspect("BSPENG.BNS", bns("J")).found, Fake()), "Imported Braille Lite Japanese " +
        "for the Blazie Engineering unit screen. Note: this one cannot be used as a voice; it runs on the unit screen.")
    // beside a voice: the voice speaks, the unit is said apart
    let both = FirmwareImport.doneMessage(inspect("both.zip", zip([("BL2ENG.BNS", bns("E")),
                                                                  ("BSPENG.BNS", bns("J"))])).found, Fake())
    check(both.hasPrefix("Imported English June 2003. The voice speaks now. Also imported Braille Lite Japanese " +
                         "for the Blazie Engineering unit screen. Note: this one cannot"), both)
    eq(FirmwareImport.doneMessage(inspect("BL2ENG.BNS", bns("E")).found, Fake()),
       "Imported English June 2003. The voice speaks now.")
    // a release voice speaks: it is no unit-tab-only release
    eq(FirmwareImport.doneMessage(inspect("two.zip", zip([("BL2ENG.BNS", bns("E")),
                                                          ("BL2HUN.BNS", bns("H"))])).found, Fake()),
       "Imported English June 2003 and Braille Lite Hungarian. The voices speak now.")
}

test("aProgramThatOnlyLooksLikeAZipIsNotFirmware") {
    // the collection's programs/unzip.BNS (found on Tomi's Android phone, 2026-10-18): Blazie's unzip program carries
    // a zip signature with a name that is no text; on Android opening it threw past the import
    let header = Data([0x50, 0x4B, 3, 4, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0,
                       0, 0xC3, 0x28, 0xFF])
    let utf8 = Data([0x50, 0x4B, 3, 4, 20, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0,
                     0, 0xC3, 0x28, 0xFF])      // ... and with the UTF-8 flag, deflated, its data no deflate stream
    for h in [header, utf8] {
        let unzip = Data(repeating: 0x18, count: 5000) + h + Data(count: 200)
        let plan = inspect("collection.zip", zip([("emulator-firmware-folder/BL2ENG.BNS", bns("E")),
                                                  ("programs/unzip.BNS", unzip)]))
        eq(plan.refusal, nil)
        eq(kinds(plan), [E])
        // ... and alone: no firmware, said in words
        let alone = inspect("unzip.zip", zip([("unzip.BNS", unzip)]))
        check(alone.refusal?.hasPrefix(FirmwareImport.noFirmwareZip) == true, alone.refusal ?? "nil")
        // ... and as the source itself, and as a source that is such a header alone
        check(inspect("unzip.BNS", unzip).refusal?.hasPrefix(FirmwareImport.noFirmwareFile) == true, "unzip.BNS")
        check(inspect("odd.zip", h + Data(count: 200)).refusal != nil, "odd.zip")
    }
}

test("aZipInAZipInTheSourceIsSaid") {
    // the import opens one level of zips; a deeper one is named, never dropped in silence (its control: unopened)
    let plan = inspect("outer.zip", zip([("BL2ENG.BNS", bns("E")),
        ("backup.zip", zip([("old/inner.zip", zip([("BL2SPA.BNS", bns("S"))]))]))]))
    eq(kinds(plan), [E])
    check(plan.notes.contains { $0.hasPrefix("backup.zip, old/inner.zip: a zip inside a zip") }, "\(plan.notes)")
    // ... and alone, in the refusal
    let alone = inspect("outer.zip", zip([("backup.zip", zip([("inner.zip", zip([("BL2SPA.BNS", bns("S"))]))]))]))
    check(alone.refusal?.contains("backup.zip, inner.zip: a zip inside a zip") == true, alone.refusal ?? "nil")
}

test("anEntryOverFourMegabytesThatIsNoZipIsSkipped") {
    // nothing that big is firmware: skipped without a word, however its bytes look
    let big = bns("S") + Data(count: 5 << 20)
    let plan = inspect("big.zip", zip([("BL2ENG.BNS", bns("E")), ("big/BL2SPA.BNS", big)], style: .stored))
    eq(kinds(plan), [E])
    eq(plan.notes, [])
}

// ---- Export to WAV (WavExport.swift): Android's WavExportTest.kt ---------------------------------------------------

test("theWavNameIsTheUnitAndTheTime") {
    var c = DateComponents()
    (c.year, c.month, c.day, c.hour, c.minute, c.second) = (2026, 10, 18, 16, 45, 33)
    let at = Calendar.current.date(from: c)!
    func name(_ v: String) -> String { WavExport.fileName(voiceName: v, at: at) }
    eq(name("en-US-speakout"), "speakout-20261018-164533.wav")
    eq(name("en-US-accentsa"), "accentsa-20261018-164533.wav")
    eq(name("en-US-mockingboard-early"), "mockingboard-early-20261018-164533.wav")
    eq(name("en-US-soundingboard"), "soundingboard-20261018-164533.wav")
    // another language keeps its tag after the unit, once when the country only repeats it
    eq(name("de-DE-braillelite"), "braillelite-de-20261018-164533.wav")
    eq(name("sk-SK-braillenspeak"), "braillenspeak-sk-20261018-164533.wav")
    eq(name("pt-BR-braillelite"), "braillelite-pt-br-20261018-164533.wav")
    eq(name("ar-braillelite"), "braillelite-ar-20261018-164533.wav")
    // a name with no unit after its tag is kept whole
    eq(name("en-US"), "en-US-20261018-164533.wav")
    // this app's voices, named as Android names them
    eq(WavExport.voiceName(key: "speakout", language: "en-US"), "en-US-speakout")
    eq(WavExport.voiceName(key: "mockingboard-early", language: "en-US"), "en-US-mockingboard-early")
    eq(WavExport.voiceName(key: "braillelite-de", language: "de-DE"), "de-DE-braillelite")
    eq(WavExport.voiceName(key: "braillenspeak-sk", language: "sk-SK"), "sk-SK-braillenspeak")
    eq(WavExport.voiceName(key: "braillelite-pt-br", language: "pt-BR"), "pt-BR-braillelite")
    eq(WavExport.voiceName(key: "braillelite-ar", language: "ar-001"), "ar-braillelite")
}

test("theWavFileIsTheRenderWithItsHeader") {
    let pcm = (0..<1001).map { Int16(truncatingIfNeeded: $0 * 77 - 30000) }
    let d = WavExport.wav(pcm, rate: 22050)
    func u32(_ at: Int) -> Int { Int(d[at]) | Int(d[at + 1]) << 8 | Int(d[at + 2]) << 16 | Int(d[at + 3]) << 24 }
    func u16(_ at: Int) -> Int { Int(d[at]) | Int(d[at + 1]) << 8 }
    eq(String(data: d.prefix(4), encoding: .ascii), "RIFF")
    eq(u32(4), 36 + 2002)
    eq(String(data: d.subdata(in: 8..<16), encoding: .ascii), "WAVEfmt ")
    eq([u32(16), u16(20), u16(22), u32(24), u32(28), u16(32), u16(34)], [16, 1, 1, 22050, 44100, 2, 16])
    eq(String(data: d.subdata(in: 36..<40), encoding: .ascii), "data")
    eq(u32(40), 2002)
    eq(d.count, 44 + 2002)
    eq((0..<1001).map { Int16(bitPattern: UInt16(u16(44 + 2 * $0))) }, pcm)
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
