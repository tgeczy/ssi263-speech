// FirmwareImport.swift -- the firmware a person brings, and what is in it: Android's FirmwareImport.kt in Swift, with
// the Accents' files added, because the Apple apps ship hollow (no firmware at all, Aicom's neither).  Nothing here
// touches the system beyond files, so the host-side test (test/FirmwareImportTests.swift) runs it as it is; the bytes
// themselves are judged by the native side (ssp_import.c: Aicom's files by sha256, then bl_firmware.c's list of
// Braille Lite releases and ssa_import.c's SPEAKOUT.HEX) behind `Identify`, by content and never by name.
//
// Only Braille Lite releases on the native side's list are taken, only GW Micro's known SPEAKOUT.HEX and only Aicom's
// own files, and never a unit's state: the app always makes its own from the firmware (Tomi, 2026-09-30).  MIT.

import Foundation

enum FirmwareImport {
    // What the native side found (ssp_import.h): ssa_import_firmware's, then Aicom's
    static let english = 0                  // the Braille Lite, English
    static let spanish = 1                  // the Braille Lite, Spanish
    static let speakout = 3                 // the GW Micro Speak-Out
    static let mockingboard = 5             // the Mockingboard: Sweet Micro Systems' file, or the toolkit disk's
    static let accentU2 = 10                // the Accent SA's program ROM
    static let accentU3 = 11                // its dictionary, part 1
    static let accentU4 = 12                // its dictionary, part 2
    static let accentMini = 13              // the Accent-mini's SPKEMS.DVC
    static let none = -1                    // no firmware in the bytes
    static let refused = -2                 // Blazie firmware, but another unit's: not one the voice can run
    static let unknown = -4                 // Braille Lite 2000 firmware, but not a release on the list
    static let otherHex = -5                // an Intel HEX file, but not the Speak-Out's SPEAKOUT.HEX
    static let notBuilt = -6                // the Mockingboard's file, but this copy of the app has no Mockingboard
    static let otherDisk = -7               // a 140 KB Apple II disk image, but not the Mockingboard toolkit's

    /// The file each found firmware is kept as, in the order an import takes them; the Braille Lite's state beside its
    /// .BNS is made on the device (`stateFiles`).
    static let files: [Int: String] = [english: "BL2ENG.BNS", spanish: "BL2SPA.BNS", speakout: "SPEAKOUT.HEX",
                                       mockingboard: "mockingboard-tts-1.1.bin", accentU2: "u2.BIN", accentU3: "u3.BIN", accentU4: "u4.BIN",
                                       accentMini: "SPKEMS.DVC"]
    static let stateFiles: [Int: String] = [english: "bl2_2003_warm.state", spanish: "bl2spa_fresh.state"]
    static let order = [english, spanish, speakout, mockingboard, accentU2, accentU3, accentU4, accentMini]
    static let accentRoms = [accentU2, accentU3, accentU4]

    static let stateSize = 786432           // battery-backed RAM + file flash: every state bl_save_state writes
    static let maxSource = 64 << 20         // an add-on is a few megabytes; firmware a few hundred kilobytes
    static let maxEntry = 4 << 20           // an update program is half a megabyte

    static let noFirmwareZip = "This zip does not contain Braille Lite, Speak-Out, Mockingboard or Accent firmware."
    static let noFirmwareFile = "This file does not contain Braille Lite, Speak-Out, Mockingboard or Accent firmware."
    /// Tomi's words, for a state picked on its own.
    static let stateFile = "This is a state file, not firmware. Please import only firmware files, or zips " +
        "containing them, with this tool."
    /// Neutral words for a store app (Tomi): the files a user owns and the packages they may come in, never where
    /// to get them, and never which package carries what.
    static let whatToChoose = "Choose firmware files you own. For the Braille Lite 2000: its update program (such " +
        "as blt2000.exe), or the BL2ENG.BNS or BL2SPA.BNS inside it. For the Speak-Out: GW Micro's SPEAKOUT.HEX, " +
        "or the speakout.zip holding it. For the Mockingboard: your own copy of the Mockingboard Developers " +
        "Toolkit disk, as its image (.dsk, .do or .po), or the mockingboard-tts-1.1.bin made from it. For the " +
        "Accents: Aicom's u2.BIN, u3.BIN and u4.BIN for the Accent SA and SPKEMS.DVC for the Accent-mini. A .zip " +
        "or .nvda-addon package containing them works too."
    static let onlyTheHex = "Only GW Micro's SPEAKOUT.HEX, as it came, can be imported for the Speak-Out."

    /// The host-side tests' controls (SSI263_IMPORT_BREAK; never set in the app): `1` looks at a zip's top only -- no
    /// folder down, no add-on layout -- so the layout tests must fail; `state` stops knowing a state file;
    /// `speakout` drops what the native side says is the Speak-Out's; `mockingboard` the Mockingboard's; `accent`
    /// drops Aicom's files.
    static var control = ProcessInfo.processInfo.environment["SSI263_IMPORT_BREAK"] ?? ""

    /// What the native side says about some bytes: the app's NativeIdentify, a fake in the tests.
    protocol Identify {
        /// The firmware in `data`, written to `out` when found (the Braille Lite's image as a .BNS, the Speak-Out's
        /// HEX, an Aicom file as it is), with its label; or a negative refusal with the reason.
        func firmware(_ data: Data, out: URL) -> (Int, String)
        /// The Braille Lite releases on the list, by label, newest first.
        func known() -> [String]
    }

    static func name(of found: Int) -> String {
        switch found {
        case english: return "English Braille Lite"
        case spanish: return "Spanish Braille Lite"
        case speakout: return "Speak-Out"
        case mockingboard: return "Mockingboard"
        case accentU2, accentU3, accentU4: return "Accent SA"
        case accentMini: return "Accent-mini"
        default: return "unit"
        }
    }

    static func languageName(_ found: Int) -> String {
        found == spanish ? "Spanish" : found == speakout ? "Speak-Out" : found == mockingboard ? "Mockingboard" : "English"
    }

    /// A unit's state, by its content: the size every state has.  Never imported, whatever its name.
    static func isState(_ data: Data) -> Bool { control != "state" && data.count == stateSize }

    /// One firmware the import will bring in.
    struct Found {
        let kind: Int                       // what it is: `english` ... `accentMini`
        let from: String                    // where it was: the file's name, or its path in the zip
        let firmware: URL                   // written into the staging folder
        let label: String                   // in words (the native side's)
    }

    /// Either `refusal` says why nothing can be imported, in words for the person holding the device, or `found`
    /// lists what will be.  `notes` are things worth saying either way.
    struct Plan {
        let found: [Found]
        let refusal: String?
        var notes: [String] = []
    }

    /// Everything a source's bytes hold, judged into `staging` (emptied first).
    static func inspect(name: String, data: Data, staging: URL, id: Identify) -> Plan {
        let fm = FileManager.default
        try? fm.removeItem(at: staging)
        try? fm.createDirectory(at: staging, withIntermediateDirectories: true)
        let sink = Sink(staging: staging, id: id)
        if ZipReader.isZip(data, at: 0) {
            do {
                try sink.zip(name: name, data: data, at: 0, nested: false)
            } catch {
                return Plan(found: [], refusal: "This zip is damaged, so nothing in it could be read. " + whatToChoose)
            }
            return sink.plan(zip: true)
        }
        if isState(data) { return Plan(found: [], refusal: stateFile) }
        sink.judge(from: name, bytes: data)
        if sink.firmware.isEmpty && sink.refused.isEmpty && sink.unknown.isEmpty, let at = ZipReader.zipStart(data),
           at > 0 {
            // An update program that is a zip behind its own code (blt2000.exe): its first entry onwards.
            if (try? sink.zip(name: name, data: data, at: at, nested: false)) != nil {
                return sink.plan(zip: false)
            }
        }
        return sink.plan(zip: false)
    }

    private final class Sink {
        struct Candidate { let kind: Int; let from: String; let file: URL; let label: String }

        let staging: URL
        let id: Identify
        var firmware: [Candidate] = []
        var refused: [String] = []
        var unknown: [String] = []
        var otherHex: [(String, String)] = []     // where, and the native side's reason
        var withReason: [(String, String)] = []   // ... the Mockingboard's in a copy without it, another disk image
        var states: [String] = []
        var deep: [String] = []
        private var n = 0

        init(staging: URL, id: Identify) {
            self.staging = staging
            self.id = id
        }

        /// One file's bytes, by the native side's eyes; true when it held firmware of any kind.
        @discardableResult
        func judge(from: String, bytes: Data) -> Bool {
            let out = staging.appendingPathComponent("candidate\(n).bin")
            n += 1
            var (kind, text) = id.firmware(bytes, out: out)
            if (control == "speakout" && kind == speakout) || (control == "mockingboard" && kind == mockingboard)
                || (control == "accent" && kind >= accentU2) {
                kind = FirmwareImport.none
                text = ""
            }
            if kind >= 0 {
                firmware.append(Candidate(kind: kind, from: from, file: out, label: text))
            } else if kind == FirmwareImport.refused {
                refused.append(from)
            } else if kind == FirmwareImport.unknown {
                unknown.append(from)
            } else if kind == FirmwareImport.otherHex {
                otherHex.append((from, text))
            } else if kind == FirmwareImport.notBuilt || kind == FirmwareImport.otherDisk {
                withReason.append((from, text))
            } else {
                return false
            }
            return true
        }

        /// The zip's entries at its top, one folder down, and where the NVDA add-ons keep their units' files
        /// (synthDrivers/_ssi263_<name>/, and the Accent SA's ROMs one folder further, accent-sa/).
        func zip(name: String, data: Data, at: Int, nested: Bool) throws {
            var zip = ZipReader(data, start: at)
            do {
                while let (e, bytes) = try zip.next(cap: maxEntry) {
                    if e.isDirectory { continue }
                    let path = e.name.replacingOccurrences(of: "\\", with: "/")
                        .trimmingCharacters(in: CharacterSet(charactersIn: "/"))
                    let parts = path.split(separator: "/").map(String.init)
                    if parts.isEmpty || parts.contains("__MACOSX") { continue }
                    let addon = (3...4).contains(parts.count) && parts[0].lowercased() == "synthdrivers"
                        && parts[1].lowercased().hasPrefix("_ssi263_")
                    let tooDeep = control == "1" ? parts.count > 1 : parts.count > 2 && !addon
                    if tooDeep {
                        let last = parts.last!.lowercased()
                        if [".bns", ".exe", ".hex", ".bin", ".dvc", ".dsk", ".do", ".po"]
                            .contains(where: { last.hasSuffix($0) }) {
                            deep.append(path)
                        }
                        continue
                    }
                    guard let bytes = bytes else { continue }
                    let from = nested ? "\(name), \(path)" : path
                    if isState(bytes) { states.append(from); continue }
                    if !judge(from: from, bytes: bytes) && !nested, let s = ZipReader.zipStart(bytes) {
                        try? self.zip(name: path, data: bytes, at: s, nested: true)
                    }
                }
            } catch {
                if !nested { throw error }
            }
        }

        func plan(zip: Bool) -> Plan {
            var notes: [String] = []
            var found: [Found] = []
            // One release per kind: the list's first (the newest) when a source holds two.
            let rank = id.known()
            func r(_ label: String) -> Int { rank.firstIndex(of: label) ?? Int.max }
            for kind in order {
                guard let c = firmware.filter({ $0.kind == kind }).min(by: { r($0.label) < r($1.label) }) else {
                    continue
                }
                found.append(Found(kind: kind, from: c.from, firmware: c.file, label: c.label))
            }
            let kept = Set(found.map { $0.firmware })
            for c in firmware where !kept.contains(c.file) {
                try? FileManager.default.removeItem(at: c.file)
                let f = found.first { $0.kind == c.kind }!
                if c.label != f.label {     // the same release twice (an update program and its .BNS) is one
                    let l = languageName(c.kind)
                    notes.append("\(c.from) is another \(l) release (\(c.label)); only \(f.from) (\(f.label)) is " +
                        "imported, as the unit holds one \(l) release at a time.")
                }
            }
            for from in unknown {
                notes.append("\(from) is Braille Lite 2000 firmware of a release this app does not know; it is left out.")
            }
            for (from, why) in otherHex + withReason { notes.append("\(from) is \(why); it is left out.") }
            if !states.isEmpty {
                let one = states.count == 1
                notes.append("\(states.joined(separator: ", ")) \(one ? "is a state file" : "are state files"), not " +
                    "firmware, and \(one ? "is" : "are") not used: this device prepares the unit's state itself " +
                    "from the firmware.")
            }
            if !found.isEmpty { return Plan(found: found, refusal: nil, notes: notes) }

            if !states.isEmpty && unknown.isEmpty && refused.isEmpty && deep.isEmpty && otherHex.isEmpty
                && withReason.isEmpty {
                let what = states.count == 1 ? "a state file (\(states[0]))"
                    : "state files (\(states.joined(separator: ", ")))"
                return Plan(found: [], refusal: "This \(zip ? "zip" : "file") holds \(what), not firmware. Please " +
                    "import only firmware files, or zips containing them, with this tool.")
            }
            var refusal: String
            if !unknown.isEmpty {
                refusal = "\(unknown.joined(separator: ", ")) \(unknown.count == 1 ? "is" : "are") Braille Lite 2000 " +
                    "firmware, but not a release this app knows, so it cannot be imported: a release for another " +
                    "country may be laid out differently, and only releases tested with this voice are taken. The " +
                    "releases this app knows: \(rank.joined(separator: "; "))."
            } else if !otherHex.isEmpty {
                refusal = otherHex.map { "\($0.0) is \($0.1)." }.joined(separator: " ") + " " + onlyTheHex
            } else if !withReason.isEmpty {
                refusal = withReason.map { "\($0.0) is \($0.1)." }.joined(separator: " ")
            } else {
                refusal = zip ? noFirmwareZip : noFirmwareFile
            }
            for from in self.refused {
                refusal += " \(from) is Blazie firmware, but not a Braille Lite 2000 release this voice can run."
            }
            if !states.isEmpty {
                refusal += " \(states.joined(separator: ", ")): a state file, not firmware; this device makes the " +
                    "unit's state itself."
            }
            if let d = deep.first {
                refusal += " Firmware is looked for only at the top of the zip and one folder down; \(d) is deeper " +
                    "than that."
            }
            return Plan(found: [], refusal: refusal + " " + whatToChoose)
        }
    }
}
