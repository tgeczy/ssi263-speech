// The app's import as the app runs it -- FirmwareInstaller.inspect and Job.commit over the real native side
// (SSI263Core's macos-arm64 slice) -- on the repository's own firmware, in a folder of its own in place of the App
// Group's: each unit's files end up in the unit folder, labelled, and SsiShared sees each voice as installed -- and
// the Mockingboard from the toolkit's disk image, in MOCKINGBOARD_DISKS (paths.local).  Then the Braille Lite unit
// tab's catalog on what was imported, and two units started (a release unit's first state made); and the tab's unit
// running on after the system stops its sound (#60).  Skips what is not there.
//
//     sh src/platforms/apple/test/test_installer.sh
//     SSI263_UNIT_BREAK=factory sh ...      the control: a release unit's first state not made, so its start must FAIL
//     SSI263_UNIT_BREAK=restart sh ...      the control: the tab's sound left stopped when the system stops it
import Foundation

let repo = URL(fileURLWithPath: CommandLine.arguments[1])
let root = FileManager.default.temporaryDirectory.appendingPathComponent("ssi263-installer-\(getpid())")
try? FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
SsiShared.containerOverride = root
var failures = 0

let disks = CommandLine.arguments.count > 2 ? URL(fileURLWithPath: CommandLine.arguments[2]) : nil

// every voice the library has (ssa_engine.h: 0 ..< SSA_ALL_VOICES, the Super Voice's 30 last) is one of the app's,
// once: the Apple column of tools/check_uniform.py's matrix
do {
    let indexes = SsiShared.voices.map { Int($0.index) }.sorted()
    let ok = indexes == Array(0..<Int(SSA_ALL_VOICES)) && SsiShared.voices.last?.key == "supervoice"
    print("\(ok ? "ok   " : "FAIL ") every voice of the library is the app's, once: \(indexes.count) of \(SSA_ALL_VOICES)")
    if !ok { failures += 1 }
    // listed as the NVDA driver lists them: GW Micro's Sounding Board right under the Speak-Out, its index unchanged
    let keys = SsiShared.voices.map { $0.key }
    let gw = keys.firstIndex(of: "speakout").map { keys.indices.contains($0 + 1) && keys[$0 + 1] == "soundingboard" }
    let sb = SsiShared.voices.first { $0.key == "soundingboard" }?.index == SSA_SOUNDINGBOARD
    print("\(gw == true && sb ? "ok   " : "FAIL ") the Sounding Board listed under the Speak-Out, still voice " +
          "\(SSA_SOUNDINGBOARD)")
    if !(gw == true && sb) { failures += 1 }
}

func run(_ label: String, _ files: [String], expect: [String], kept: [String] = [], from base: URL? = nil) {
    let urls = files.map { (base ?? repo.appendingPathComponent("firmware")).appendingPathComponent($0) }
    guard urls.allSatisfy({ FileManager.default.fileExists(atPath: $0.path) }) else {
        print("skip  \(label): its firmware is not there")
        return
    }
    do {
        let plan = try FirmwareInstaller.inspect(urls)
        if let r = plan.refusal { throw FirmwareInstaller.Failure(message: "refused: " + r) }
        let job = FirmwareInstaller.Job(plan: plan)
        let labels = try job.commit()
        let installed = SsiShared.voices.filter(SsiShared.installed).map { $0.key }
        let unit = SsiShared.unitFolder!
        let ok = expect.allSatisfy(installed.contains)
            && kept.allSatisfy { FileManager.default.fileExists(atPath: unit.appendingPathComponent($0).path) }
        print("\(ok ? "ok   " : "FAIL ") \(label): \(labels.joined(separator: "; ")); installed: \(installed)")
        if !ok { failures += 1 }
    } catch {
        print("FAIL  \(label): \(error.localizedDescription)")
        failures += 1
    }
}

run("the Speak-Out", ["gw-micro-speakout/SPEAKOUT.HEX"], expect: ["speakout"])
run("the Accents", ["aicom-accent-sa/u2.BIN", "aicom-accent-sa/u3.BIN", "aicom-accent-sa/u4.BIN",
                    "aicom-accent-mini/SPKEMS.DVC"], expect: ["accentsa", "accentmini"])
run("the English Braille Lite", ["blazie/BL2ENG.BNS"], expect: ["braillelite"])
run("the Mockingboard's file", ["sweet-micro-mockingboard/mockingboard-tts-1.1.bin"], expect: ["mockingboard"])
run("the Slovak Braille 'n Speak 2000 (its state made with the English recipe)", ["blazie/bns2000/BS2SLL.BNS"],
    expect: ["braillenspeak-sk"])
run("the German Braille Lite", ["blazie/german/Lite2000.BNS"], expect: ["braillelite-de"])
run("the Italian Braille Lite", ["blazie/italian/Lite2000.BNS"], expect: ["braillelite-it"])
// the release table's: a release voice (its state by its own recipe), and a release kept for the unit tab alone
run("the Hungarian Braille Lite (a release voice)", ["blazie/bl2000/BL2HUN.BNS"], expect: ["braillelite-hu"],
    kept: ["BL2HUN.BNS", "bl2_hu_1999_07.state", "BL2HUN.BNS.label"])
run("the Japanese Braille Lite (the unit tab's alone, beside the Hungarian)", ["blazie/bl2000/BL2JPN.BNS"],
    expect: ["braillelite-hu"], kept: ["units/bl2_ja.BNS", "units/bl2_ja.BNS.label"])
// GW Micro's Sounding Board (0.8.5): its two drivers chosen as two files, then sb.zip as GW Micro gave it out (each
// checked to speak by the commit); one driver alone, or one with a byte changed, is refused
let sbDir = "gw-micro-sounding-board/"
run("the Sounding Board's two drivers, chosen as two files", [sbDir + "SB.COM", sbDir + "SBLOAD.COM"],
    expect: ["soundingboard"], kept: ["SBLOAD.COM", "SB.COM", "SBLOAD.COM.label"])
if let unit = SsiShared.unitFolder {
    for f in ["SBLOAD.COM", "SB.COM", "SBLOAD.COM.label"] {
        try? FileManager.default.removeItem(at: unit.appendingPathComponent(f))
    }
}
run("the Sounding Board from sb.zip", [sbDir + "sb.zip"], expect: ["soundingboard"],
    kept: ["SBLOAD.COM", "SB.COM", "SBLOAD.COM.label"])
func refuses(_ label: String, _ urls: [URL], _ words: String) {
    guard urls.allSatisfy({ FileManager.default.fileExists(atPath: $0.path) }) else {
        print("skip  \(label): its firmware is not there")
        return
    }
    let plan = try? FirmwareInstaller.inspect(urls)
    let ok = plan?.found.isEmpty == true && plan?.refusal?.contains(words) == true
    print("\(ok ? "ok   " : "FAIL ") \(label): \(plan?.refusal ?? "no plan")")
    if !ok { failures += 1 }
}
let sbCom = repo.appendingPathComponent("firmware/" + sbDir + "SB.COM")
refuses("SB.COM alone", [sbCom], "but its SBLOAD.COM is needed too")
if var changed = try? Data(contentsOf: sbCom) {
    changed[100] ^= 1
    let copy = root.appendingPathComponent("changed/SB.COM")
    try? FileManager.default.createDirectory(at: copy.deletingLastPathComponent(), withIntermediateDirectories: true)
    try? changed.write(to: copy)
    refuses("SB.COM with one byte changed", [copy], "does not contain")
}

// Automated Functions' SmarTalk (0.9.0): the repository's S_TALK.EXE alone, then a zip holding it where the NVDA
// add-on does (checked to speak by the commit); a copy with one byte changed is refused
let stExe = repo.appendingPathComponent("firmware/automated-functions-smartalk/S_TALK.EXE")
// the repository's file's sha256 and size: Setup's row says the size, the import's words for another copy both
var stWhich = ""
if let exe = try? Data(contentsOf: stExe) {
    var d = [UInt8](repeating: 0, count: 32)
    exe.withUnsafeBytes { blv_sha256($0.bindMemory(to: UInt8.self).baseAddress, exe.count, &d) }
    stWhich = "(\(exe.count.formatted()) bytes, SHA-256 \(d.map { String(format: "%02x", $0) }.joined()))"
    let ok = SsiShared.voices.first { $0.key == "smartalk" }?.bring.contains("(\(exe.count.formatted()) bytes)") == true
    print("\(ok ? "ok   " : "FAIL ") the SmarTalk's size, as Setup's row says it: \(exe.count) bytes")
    if !ok { failures += 1 }
}
run("the SmarTalk's S_TALK.EXE", ["automated-functions-smartalk/S_TALK.EXE"], expect: ["smartalk"],
    kept: ["S_TALK.EXE", "S_TALK.EXE.label"])
if FileManager.default.fileExists(atPath: stExe.path), let unit = SsiShared.unitFolder {
    for f in ["S_TALK.EXE", "S_TALK.EXE.label"] {
        try? FileManager.default.removeItem(at: unit.appendingPathComponent(f))
    }
    let tree = root.appendingPathComponent("st-zip/synthDrivers/_ssi263_unified/_ssi263_smartalk")
    try? FileManager.default.createDirectory(at: tree, withIntermediateDirectories: true)
    try? FileManager.default.copyItem(at: stExe, to: tree.appendingPathComponent("S_TALK.EXE"))
    let zip = Process()
    zip.executableURL = URL(fileURLWithPath: "/usr/bin/zip")
    zip.currentDirectoryURL = root.appendingPathComponent("st-zip")
    zip.arguments = ["-qr", "../smartalk.zip", "synthDrivers"]
    try? zip.run()
    zip.waitUntilExit()
    run("the SmarTalk from a zip, in the add-on's folder", ["smartalk.zip"], expect: ["smartalk"],
        kept: ["S_TALK.EXE", "S_TALK.EXE.label"], from: root)
}
if var changed = try? Data(contentsOf: stExe) {
    changed[1000] ^= 1
    let copy = root.appendingPathComponent("changed/S_TALK.EXE")
    try? FileManager.default.createDirectory(at: copy.deletingLastPathComponent(), withIntermediateDirectories: true)
    try? changed.write(to: copy)
    refuses("S_TALK.EXE with one byte changed: the copy it runs named", [copy],
            "S_TALK.EXE is not the SmarTalk's S_TALK.EXE 1.21, the copy this voice runs " + stWhich + ".")
}

// the Artic SynPhonix (0.9.5): the repository's VEST.COM (its serial 99999999) alone, a copy with another card's
// serial (known by its content, never a hash), then a zip holding it where the NVDA add-on does -- each checked to
// speak by the commit (ssa_probe), and kept as it came
let spCom = repo.appendingPathComponent("firmware/artic-synphonix/VEST.COM")
func spKept(_ label: String, _ want: Data) {
    let got = SsiShared.unitFolder.flatMap { try? Data(contentsOf: $0.appendingPathComponent("VEST.COM")) }
    let ok = got == want
    print("\(ok ? "ok   " : "FAIL ") \(label): VEST.COM kept as it came")
    if !ok { failures += 1 }
}
func spForget() {
    for f in ["VEST.COM", "VEST.COM.label"] {
        try? SsiShared.unitFolder.map { try FileManager.default.removeItem(at: $0.appendingPathComponent(f)) }
    }
}
if let com = try? Data(contentsOf: spCom) {
    run("the SynPhonix's VEST.COM", ["artic-synphonix/VEST.COM"], expect: ["synphonix"],
        kept: ["VEST.COM", "VEST.COM.label"])
    spKept("the SynPhonix's VEST.COM", com)
    spForget()
    let other = Data(com).replacing(Data("S/N 999999".utf8), with: Data("S/N 123456".utf8))
    let dir = root.appendingPathComponent("sp-other")
    try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    try? other.write(to: dir.appendingPathComponent("VEST.COM"))
    run("a VEST.COM with another card's serial", ["sp-other/VEST.COM"], expect: ["synphonix"],
        kept: ["VEST.COM", "VEST.COM.label"], from: root)
    spKept("a VEST.COM with another card's serial", other)
    spForget()
    let tree = root.appendingPathComponent("sp-zip/synthDrivers/_ssi263_unified/_ssi263_synphonix")
    try? FileManager.default.createDirectory(at: tree.appendingPathComponent("licenses"),
                                             withIntermediateDirectories: true)
    try? com.write(to: tree.appendingPathComponent("VEST.COM"))
    try? Data("Artic Technologies' VEST.COM, its notice".utf8)
        .write(to: tree.appendingPathComponent("licenses/Artic-VEST-notice.txt"))
    let zip = Process()
    zip.executableURL = URL(fileURLWithPath: "/usr/bin/zip")
    zip.currentDirectoryURL = root.appendingPathComponent("sp-zip")
    zip.arguments = ["-qr", "../synphonix.zip", "synthDrivers"]
    try? zip.run()
    zip.waitUntilExit()
    run("the SynPhonix from a zip, in the add-on's folder", ["synphonix.zip"], expect: ["synphonix"],
        kept: ["VEST.COM", "VEST.COM.label"], from: root)
    spKept("the SynPhonix from a zip", com)
} else {
    print("skip  the SynPhonix: firmware/artic-synphonix/VEST.COM is not there")
}

// Speech Systems' Super Voice (0.9.5), when the person's copy is in firmware/speech-systems-super-voice (never in the
// repository): the program, the real disk image and the zip as the archive keeps it, each imported through the real
// native side (sv_firmware_from), heard by ssa_probe, and the program alone kept
let svDir = repo.appendingPathComponent("firmware/speech-systems-super-voice")
let svZip = "Super Voice (Speech Systems).zip"
if let prog = try? Data(contentsOf: svDir.appendingPathComponent("SUPERTOK.BIN")) {
    for (label, file) in [("the Super Voice's SUPERTOK.BIN", "SUPERTOK.BIN"),
                          ("the Super Voice from its disk image, SUPERV.DSK", "SUPERV.DSK"),
                          ("the Super Voice from the zip holding its disk", svZip)] {
        guard FileManager.default.fileExists(atPath: svDir.appendingPathComponent(file).path) else {
            print("skip  \(label): no \(file)")
            continue
        }
        for f in ["SUPERTOK.BIN", "SUPERTOK.BIN.label"] {
            try? SsiShared.unitFolder.map { try FileManager.default.removeItem(at: $0.appendingPathComponent(f)) }
        }
        run(label, ["speech-systems-super-voice/" + file], expect: ["supervoice"],
            kept: ["SUPERTOK.BIN", "SUPERTOK.BIN.label"])
        let got = SsiShared.unitFolder.flatMap { try? Data(contentsOf: $0.appendingPathComponent("SUPERTOK.BIN")) }
        print("\(got == prog ? "ok   " : "FAIL ") \(label): the program alone kept, byte for byte")
        if got != prog { failures += 1 }
    }
} else {
    print("skip  the Super Voice: no firmware/speech-systems-super-voice/SUPERTOK.BIN")
}

// the confirmation's words (Android's): a language update takes a few seconds, and one already here is replaced;
// the unit tab's alone is kept for it
func confirms(_ label: String, _ file: String, _ words: [String]) {
    let url = repo.appendingPathComponent("firmware/" + file)
    guard FileManager.default.fileExists(atPath: url.path), let plan = try? FirmwareInstaller.inspect([url]) else {
        print("skip  \(label): its firmware is not there")
        return
    }
    let said = FirmwareInstaller.confirmation(plan)
    let ok = words.allSatisfy(said.contains)
    print("\(ok ? "ok   " : "FAIL ") \(label): \(said)")
    if !ok { failures += 1 }
}

// a unit already here as it is is left alone (0.9.5, Android's rule): the same file again, with its state the one its
// release makes (the real blv_state_check), prepares nothing and asks nothing -- a Blazie unit, a release voice, the
// unit tab's alone, a voice taken as it is, a two-file voice, the add-on's folder.  SSI263_IMPORT_BREAK=same must
// FAIL these.
func alreadyHere(_ label: String, _ files: [String], from base: URL? = nil, count: Int = 1) {
    let urls = files.map { (base ?? repo.appendingPathComponent("firmware")).appendingPathComponent($0) }
    guard urls.allSatisfy({ FileManager.default.fileExists(atPath: $0.path) }),
          let plan = try? FirmwareInstaller.inspect(urls) else {
        print("skip  \(label): its firmware is not there")
        return
    }
    let name = FirmwareImport.and(urls.map { $0.lastPathComponent })
    let said = FirmwareImport.nothingNew(name, plan)
    let ok = plan.refusal == nil && plan.found.isEmpty && plan.here.count == count
        && said.hasPrefix("Everything in \(name) is already here, unchanged: ") && said.contains("Nothing was imported.")
    print("\(ok ? "ok   " : "FAIL ") \(label): \(plan.found.isEmpty ? said : "found \(plan.found.map { $0.label })")")
    if !ok { failures += 1 }
}
alreadyHere("the English Braille Lite again", ["blazie/BL2ENG.BNS"])
alreadyHere("the Hungarian again (a release voice)", ["blazie/bl2000/BL2HUN.BNS"])
alreadyHere("the Japanese again (the unit tab's alone)", ["blazie/bl2000/BL2JPN.BNS"])
alreadyHere("the Speak-Out and the Accents again", ["gw-micro-speakout/SPEAKOUT.HEX", "aicom-accent-sa/u2.BIN",
            "aicom-accent-sa/u3.BIN", "aicom-accent-sa/u4.BIN", "aicom-accent-mini/SPKEMS.DVC"], count: 5)
alreadyHere("the Sounding Board's sb.zip again", [sbDir + "sb.zip"])
alreadyHere("the SynPhonix from the add-on's folder again", ["synphonix.zip"], from: root)
alreadyHere("the Super Voice's zip again", ["speech-systems-super-voice/" + svZip])

// ... and one whose state is missing, or not the one its release makes, is prepared again: a repair, which the
// confirmation says replaces the one here
if let unit = SsiShared.unitFolder {
    let state = unit.appendingPathComponent("bl2_hu_1999_07.state")
    let aside = root.appendingPathComponent("hu.state")
    if (try? FileManager.default.moveItem(at: state, to: aside)) != nil {
        confirms("the Hungarian with no state: prepared again, replacing the one here", "blazie/bl2000/BL2HUN.BNS",
                 ["takes a few seconds.", "The Braille Lite 2000 (Hungarian) firmware already here will be replaced."])
        if var bad = try? Data(contentsOf: aside) {
            bad[bad.count / 2] ^= 1
            try? bad.write(to: state)
            confirms("the Hungarian with a state its release does not make: prepared again", "blazie/bl2000/BL2HUN.BNS",
                     ["takes a few seconds.", "already here will be replaced."])
        }
        try? FileManager.default.removeItem(at: state)
        try? FileManager.default.moveItem(at: aside, to: state)
    }
}

// A Blazie firmware collection zip (FIRMWARE_COLLECTION, paths.local; never in the repository), judged as the app
// judges it, every folder of it: every Blazie voice and every unit-tab release the table lists from one import,
// each once, the units no app runs and the programs left out -- and no unit-tab release said to speak.  The units
// this run imported above are already here, so they are counted from `here` (0.9.5) beside those `found`.
if let c = ProcessInfo.processInfo.environment["SSI263_FIRMWARE_COLLECTION"], !c.isEmpty,
   FileManager.default.fileExists(atPath: c) {
    do {
        let plan = try FirmwareInstaller.inspect([URL(fileURLWithPath: c)])
        let id = NativeIdentify()
        let rows = (0..<blv_firmware_count()).filter { blv_firmware_language($0) == BLV_FW_NONE }
        let unitRows = Set(rows.filter { blv_release_voice_key($0) == nil }.map { FirmwareImport.release + Int($0) })
        let voiceRows = Set(rows.filter { blv_release_voice_key($0) != nil }.map { FirmwareImport.release + Int($0) })
        let all = plan.found + plan.here
        let kinds = all.map { $0.kind }
        let units = kinds.filter { FirmwareImport.isUnitOnly($0, id) }
        let missing = FirmwareImport.blazie.subtracting(kinds)
        let ok = plan.refusal == nil && missing.isEmpty && kinds.count == Set(kinds).count
            && Set(units) == Set(kinds).intersection(unitRows) && Set(units).isSubset(of: unitRows)
            && !all.contains { $0.from.hasPrefix("programs/") }
        print("\(ok ? "ok   " : "FAIL ") the firmware collection: \(kinds.count) found (\(plan.here.count) here), " +
              "\(kinds.filter { voiceRows.contains($0) }.count) of \(voiceRows.count) language rows, " +
              "\(units.count) of \(unitRows.count) unit-tab releases; missing \(missing.sorted()); " +
              "\(plan.notes.count) notes" + (plan.refusal.map { "; refused: " + $0 } ?? ""))
        let absent = unitRows.subtracting(units).sorted().compactMap { id.release($0 - FirmwareImport.release)?.file }
        if !absent.isEmpty { print("        unit-tab releases not in it: \(absent.joined(separator: ", "))") }
        for n in plan.notes { print("        note: \(n)") }
        print("        the done words: " + FirmwareImport.doneMessage(plan.found, id).prefix(160) + "...")
        if !ok { failures += 1 }
    } catch {
        print("FAIL  the firmware collection: \(error.localizedDescription)")
        failures += 1
    }
} else {
    print("skip  the firmware collection: FIRMWARE_COLLECTION (paths.local) is not set")
}

if let d = disks {
    run("the Mockingboard from the toolkit's disk image", ["Sweet Micro Systems Mockingboard Developers toolkit 1984.dsk"],
        expect: ["mockingboard"], from: d)
    run("the early Mockingboard from Mockingboard disk 1's image", ["mockingboard1.dsk"],
        expect: ["mockingboard-early"], from: d)
} else {
    print("skip  the toolkit's disk image: MOCKINGBOARD_DISKS (paths.local) is not set")
}

// The Braille Lite unit tab on what was imported: the catalog finds each Blazie unit in the unit folder (the shipped
// ones under their flat names, the releases by content, units/ too), and a release unit's first start makes its
// state and runs: the unit boots and speaks (its PCM not silent within its first seconds).  No sound is played.
UnitCatalog.savedOverride = root.appendingPathComponent("saved", isDirectory: true)
func unitCase(_ label: String, _ ok: Bool, _ detail: String) {
    print("\(ok ? "ok   " : "FAIL ") \(label): \(detail)")
    if !ok { failures += 1 }
}
let entries = UnitCatalog.list()
let want = ["bl-en", "bl-de", "bl-it", "bns-sk", "bl2_hu_1999_07", "bl2_ja"].filter { k in
    k == "bl2_ja" || k == "bl2_hu_1999_07" ? FileManager.default.fileExists(atPath: repo.path + "/firmware/blazie/bl2000")
        : true
}
let keys = entries.map { $0.key }
unitCase("the unit tab's units", want.allSatisfy(keys.contains), keys.joined(separator: ", "))
for key in ["bl2_hu_1999_07", "bl-en"] {
    guard let e = entries.first(where: { $0.key == key }) else { continue }
    do {
        let state = try UnitCatalog.prepare(e)
        var err = [CChar](repeating: 0, count: 300)
        guard let u = emu_create(e.unit.kind, UnitCatalog.firmware(e), state, 22050, 0, &err, Int32(err.count)) else {
            throw UnitCatalog.Failure(message: String(cString: err))
        }
        var pcm = [Int16](repeating: 0, count: 22050)
        var loud = 0
        for _ in 0..<8 where loud == 0 {
            emu_render(u, &pcm, Int32(pcm.count))
            loud = pcm.reduce(0) { max($0, abs(Int($1))) }
        }
        emu_destroy(u)
        unitCase("the \(e.name) starts from \(state.map { URL(fileURLWithPath: $0).lastPathComponent } ?? "cold")",
                 loud > 1000, "peak \(loud) in its first seconds")
    } catch {
        unitCase("the \(e.name) starts", false, error.localizedDescription)
    }
}

// Files into a unit's flash (UnitFiles, the tab's "Load programs and files into flash"), on its saved memory as the
// tab leaves it switched off: a text file added; the same name again found there (so the first was written into the
// saved state) and nothing changed; replaced when asked; the old memory kept beside it; the unit starting from it.
if let e = entries.first(where: { $0.key == "bl-en" }), let saved = UnitCatalog.savedPath(e) {
    do {
        try FileManager.default.createDirectory(at: saved.deletingLastPathComponent(), withIntermediateDirectories: true)
        var err = [CChar](repeating: 0, count: 300)
        guard let u = emu_create(e.unit.kind, UnitCatalog.firmware(e), try UnitCatalog.prepare(e), 22050, 0, &err,
                                 Int32(err.count)), emu_save(u, saved.path) == 1 else {
            throw UnitCatalog.Failure(message: "the unit could not be saved: " + String(cString: err))
        }
        emu_destroy(u)
        let text = UnitFiles.File(name: "hello.txt", data: Data("Hello from the Mac.\n".utf8), modified: Date())
        let program = UnitFiles.File(name: "GAME.BNS", data: Data([0x18, 0x00, 0x41, 0x00, 0x7e]), modified: Date())
        let cp = e.unit.codepage
        let first = UnitFiles.load([text, program], into: saved.path, codepage: cp, replace: false)
        unitCase("files into the \(e.name)'s flash", first == .loaded(added: 2, replaced: 0), "\(first)")
        let again = UnitFiles.load([text], into: saved.path, codepage: cp, replace: false)
        var isExists = false
        if case .exists(let names) = again { isExists = names.count == 1 }
        unitCase("the same name again: asked, nothing changed", isExists, "\(again)")
        let replaced = UnitFiles.load([text], into: saved.path, codepage: cp, replace: true)
        unitCase("replaced when asked", replaced == .loaded(added: 0, replaced: 1), "\(replaced)")
        unitCase("the memory as it was kept beside it",
                 FileManager.default.fileExists(atPath: saved.path + ".before-import"), saved.lastPathComponent)
        guard let v = emu_create(e.unit.kind, UnitCatalog.firmware(e), saved.path, 22050, 0, &err, Int32(err.count))
        else { throw UnitCatalog.Failure(message: String(cString: err)) }
        var pcm = [Int16](repeating: 0, count: 22050)
        var loud = 0
        for _ in 0..<8 where loud == 0 {
            emu_render(v, &pcm, Int32(pcm.count))
            loud = pcm.reduce(0) { max($0, abs(Int($1))) }
        }
        emu_destroy(v)
        unitCase("the unit starts with its new files", loud > 1000, "peak \(loud)")
    } catch {
        unitCase("files into the \(e.name)'s flash", false, error.localizedDescription)
    }
}

// The tab's unit running as the tab runs it (UnitRunner, its sound muted), then its audio engine stopped as the system
// stops it -- an output device with another rate or channel count stops the engine and posts
// AVAudioEngineConfigurationChange (its documentation) -- and the unit must run on: it runs only while its sound is
// pulled (#60).
if let e = entries.first(where: { $0.key == "bl-en" }) {
    let runner = UnitRunner()
    do {
        try runner.start(e, volume: 0, quick: false, doubleSpeed: false,
                         idle: UnitRunner.Idle(sound: 0, keepOpen: 0, popClick: false, tick: false))
        runner.engine.mainMixerNode.outputVolume = 0
        RunLoop.main.run(until: Date() + 0.5)
        let ran = runner.rendered
        runner.engine.stop()
        NotificationCenter.default.post(name: .AVAudioEngineConfigurationChange, object: runner.engine)
        RunLoop.main.run(until: Date() + 0.5)
        let stopped = runner.rendered
        RunLoop.main.run(until: Date() + 0.5)
        unitCase("the \(e.name) runs on after the system stops its sound", ran > 0 && runner.rendered > stopped,
                 "\(ran) frames before, \(runner.rendered - stopped) in the half second after")
    } catch {
        unitCase("the \(e.name) runs in the tab", false, error.localizedDescription)
    }
    runner.stop()
}

try? FileManager.default.removeItem(at: root)
print(failures == 0 ? "the import installs every unit, and the unit tab runs them" : "FAILED: \(failures)")
exit(failures == 0 ? 0 : 1)
