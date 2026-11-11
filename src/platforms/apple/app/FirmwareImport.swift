// FirmwareImport.swift -- the firmware a person brings, and what is in it: Android's FirmwareImport.kt in Swift, with
// the Accents' files added, because the Apple apps ship hollow (no firmware at all, Aicom's and Sweet Micro's
// neither).  Nothing here
// touches the system beyond files, so the host-side test (test/FirmwareImportTests.swift) runs it as it is; the bytes
// themselves are judged by the native side (ssp_import.c: Aicom's files by sha256, then bl_firmware.c's list of
// Braille Lite releases and ssa_import.c's SPEAKOUT.HEX) behind `Identify`, by content and never by name.
//
// Only the Blazie releases on the native side's list are taken (the Braille Lite in English, Spanish, German and
// Italian, the Slovak Braille 'n Speak 2000, and every other release the table lists: a release voice's, Hungarian
// and the others, or one the Braille Lite unit tab runs alone), only GW Micro's known SPEAKOUT.HEX and the Sounding
// Board's two drivers (SBLOAD.COM and SB.COM 2.7, together or not at all), the Mockingboards' two known files and only
// Aicom's own files, and never a unit's state: the app always makes its own from the firmware (Tomi, 2026-09-30).
// MIT.

import Foundation

enum FirmwareImport {
    // What the native side found (ssp_import.h): ssa_import_firmware's -- the app's voice for a Blazie release
    // (ssa_blazie_voice), the Speak-Out's, a Mockingboard's -- then Aicom's
    static let english = 0                  // the Braille Lite, English
    static let spanish = 1                  // the Braille Lite, Spanish
    static let speakout = 3                 // the GW Micro Speak-Out
    static let mockingboard = 5             // the Mockingboard 1.1: Sweet Micro Systems' file, or the toolkit disk's
    static let mockingboardEarly = 6        // the early Mockingboard: its file, or Mockingboard disk 1's
    static let german = 7                   // the Braille Lite, German (0.8)
    static let italian = 8                  // the Braille Lite, Italian
    static let slovak = 9                   // the Braille 'n Speak 2000, Slovak
    static let accentU2 = 10                // the Accent SA's program ROM
    static let accentU3 = 11                // its dictionary, part 1
    static let accentU4 = 12                // its dictionary, part 2
    static let accentMini = 13              // the Accent-mini's SPKEMS.DVC
    static let soundingboard = 27           // GW Micro's Sounding Board (ssa_engine.h SSA_SOUNDINGBOARD; 0.8.5): two
    static let sbLoad = 90                  // files -- SBLOAD.COM (ssa_import.h SSA_FW_SBLOAD) and
    static let sbCom = 91                   // SB.COM (SSA_FW_SB): found apart, imported only together
    static let smartalk = 28                // Automated Functions' SmarTalk (SSA_SMARTALK; 0.9.0): its S_TALK.EXE
    static let release = 100                // + a release's row (ssp_import.h's SSP_FW_RELEASE): no shipped voice
    static let none = -1                    // no firmware in the bytes
    static let refused = -2                 // Blazie firmware, but another unit's: not one the voice can run
    static let unknown = -4                 // Braille Lite 2000 firmware, but not a release on the list
    static let otherHex = -5                // an Intel HEX file, but not the Speak-Out's SPEAKOUT.HEX
    static let notBuilt = -6                // a Mockingboard's file, but this copy of the app has no Mockingboard
    static let otherDisk = -7               // a 140 KB Apple II disk image, but not one of the two Mockingboard ones

    /// The file each found firmware is kept as (ssa_engine.c's names: 0.8's Blazie units under the NVDA add-on's
    /// flat ones, the German and Italian disks both calling theirs Lite2000.BNS); a Blazie unit's state beside its
    /// .BNS is made on the device (`stateFiles`).
    static let files: [Int: String] = [english: "BL2ENG.BNS", spanish: "BL2SPA.BNS", german: "Lite2000_de.BNS",
                                       italian: "Lite2000_it.BNS", slovak: "BS2SLL.BNS", speakout: "SPEAKOUT.HEX",
                                       mockingboard: "mockingboard-tts-1.1.bin",
                                       mockingboardEarly: "mockingboard-tts-early.bin",
                                       accentU2: "u2.BIN", accentU3: "u3.BIN", accentU4: "u4.BIN",
                                       accentMini: "SPKEMS.DVC", soundingboard: "SBLOAD.COM",
                                       smartalk: "S_TALK.EXE"]
    /// A voice of two files (the Sounding Board's drivers): each file's kind as the native side finds it, and the
    /// names they are kept under, in the same order (the first is `files`').
    static let parts: [Int: [Int]] = [soundingboard: [sbLoad, sbCom]]
    static let partFiles: [Int: [String]] = [soundingboard: ["SBLOAD.COM", "SB.COM"]]
    static let soundingboardLabel = "GW Micro Sounding Board 2.7: SBLOAD.COM and SB.COM"
    static let stateFiles: [Int: String] = [english: "bl2_2003_warm.state", spanish: "bl2spa_fresh.state",
                                            german: "lite2000_de_fresh.state", italian: "lite2000_it_fresh.state",
                                            slovak: "bs2sll_fresh.state"]
    /// The order an import takes them in (Android's IMPORTED, with the Sounding Board after the Speak-Out as Setup lists
    /// them, then Aicom's).
    static let order = [english, spanish, german, italian, slovak, speakout, soundingboard, mockingboard,
                        mockingboardEarly, smartalk, accentU2, accentU3, accentU4, accentMini]
    /// The Blazie units: a state made from each one's firmware.
    static let blazie: Set<Int> = [english, spanish, german, italian, slovak]
    static let accentRoms = [accentU2, accentU3, accentU4]

    static let stateSize = 786432           // battery-backed RAM + file flash: the state bl_save_state writes at first
    /// Every size a state has (bl_board.h bl_save_state): the RAM's first 256 KB or all 768 KB (once the unit's files
    /// reach past 7FFFFh; the Spanish setup fills it), then the file flash's first 512 KB or all 2 MB.
    static let stateSizes: Set<Int> = [stateSize, 1310720, 2359296, 2883584]
    static let stateTail = 64               // ... and the emulator's clock after them (bl_clock.h, bl_rtc6242.h)
    static let stateTails: [[UInt8]] = [Array("BLCLOCK1".utf8), Array("BLR6242".utf8) + [1]]
    static let maxSource = 64 << 20         // an add-on is a few megabytes; firmware a few hundred kilobytes
    static let maxEntry = 4 << 20           // an update program is half a megabyte
    static let maxInner = 32 << 20          // a zip in the source: the add-on (5.5 MB in 0.8.0), anyone's own archive

    static let noFirmwareZip = "This zip does not contain Braille Lite, Braille 'n Speak, Speak-Out, Sounding Board, " +
        "SmarTalk, Mockingboard or Accent firmware."
    static let noFirmwareFile = "This file does not contain Braille Lite, Braille 'n Speak, Speak-Out, Sounding " +
        "Board, SmarTalk, Mockingboard or Accent firmware."
    /// Tomi's words, for a state picked on its own.
    static let stateFile = "This is a state file, not firmware. Please import only firmware files, or zips " +
        "containing them, with this tool."
    /// Neutral words for a store app (Tomi): the files a user owns and the packages they may come in, never where
    /// to get them, and never which package carries what.
    static let whatToChoose = "Choose firmware files you own. For the Braille Lite 2000: its update program (such as " +
        "blt2000.exe), or the BL2ENG.BNS (English), BL2SPA.BNS (Spanish) or Lite2000.BNS (German, Italian) inside " +
        "it, or one of its language updates (BL2HUN.BNS, BL2POL.BNS and the others). For the Slovak Braille 'n Speak " +
        "2000: BS2SLL.BNS. For the Speak-Out: GW Micro's SPEAKOUT.HEX, or the speakout.zip holding it. For the " +
        "Sounding Board: GW Micro's sb.zip, or the SBLOAD.COM and SB.COM inside it (both are needed). For the " +
        "SmarTalk: Automated Functions' S_TALK.EXE 1.21 (36,882 bytes), from the SmarTalk V1.21 disk, or a zip " +
        "holding it. For the Mockingboard: your own copy of the Mockingboard Developers Toolkit disk, as its image " +
        "(.dsk, .do or .po), or the mockingboard-tts-1.1.bin made from it; for the early Mockingboard, your own copy " +
        "of Mockingboard disk 1, or the mockingboard-tts-early.bin made from it. For the Accents: Aicom's u2.BIN, " +
        "u3.BIN and u4.BIN for the Accent SA and SPKEMS.DVC for the Accent-mini. A .zip or .nvda-addon package " +
        "containing them works too, with them in any of its folders."
    /// A zip inside a zip in the source: the import opens one level of zips, so its firmware is not looked at.
    static func unopened(_ what: String) -> String {
        "\(what): a zip inside a zip in this one, which the import does not open, so it is left out; unzip it once, " +
            "and import it or the zip around it again."
    }
    static let onlyTheHex = "Only GW Micro's SPEAKOUT.HEX, as it came, can be imported for the Speak-Out."
    /// A file called S_TALK.EXE that is not the copy the SmarTalk runs: which one is, by its size and sha256 -- said
    /// only here, at the import (Tomi, 2026-10-25: Setup's list names files, a hash read there every time is a wall).
    /// The name only words this; the firmware itself is found by its content alone.
    static func notTheSmarTalk(_ sha256: String) -> String {
        "not the SmarTalk's S_TALK.EXE 1.21, the copy this voice runs (36,882 bytes, SHA-256 \(sha256))"
    }
    /// The file's own name, from a `from` (a path in a zip, or "outer.zip, its/path" for a zip in it).
    static func baseName(_ from: String) -> String {
        String(from.split(separator: "/").last.map { $0.split(separator: ",").last ?? $0 } ?? Substring(from))
            .trimmingCharacters(in: .whitespaces)
    }

    /// The host-side tests' controls (SSI263_IMPORT_BREAK; never set in the app): `1` looks at a zip's top only -- no
    /// folder below it -- so the folder tests must fail; `unopened` drops a zip inside a zip in the source in silence;
    /// `state` stops knowing a state file; `state-sizes` knows a state by its first size alone, as before the larger
    /// states;
    /// `speakout` drops what the native side says is the Speak-Out's; `mockingboard` both Mockingboards'; `accent`
    /// drops Aicom's files; `release` the releases with no shipped voice; `soundingboard` the Sounding Board's two
    /// drivers; `smartalk` the SmarTalk's S_TALK.EXE; `smartalkname` the words for another file called S_TALK.EXE.
    static var control = ProcessInfo.processInfo.environment["SSI263_IMPORT_BREAK"] ?? ""

    /// What the native side says about some bytes: the app's NativeIdentify, a fake in the tests.
    protocol Identify {
        /// The firmware in `data`, written to `out` when found (a Blazie unit's image as a .BNS, the Speak-Out's HEX,
        /// a Mockingboard's file, an Aicom file as it is), with its label; or a negative refusal with the reason.
        func firmware(_ data: Data, out: URL) -> (Int, String)
        /// The Blazie releases on the list, by label, newest first.
        func known() -> [String]
        /// A release found as `release` + its row: the table's words for it (blv_release_*).
        func release(_ row: Int) -> Release?
        /// The sha256 (hex) of the one S_TALK.EXE the SmarTalk runs (ssp_smartalk_sha256), for the words when another
        /// file of its name is chosen.
        func smartalkSha256() -> String
    }

    /// A release with no shipped voice, as the table has it.
    struct Release {
        let id: String                      // "bl2_hu_1999_07": its files' names
        let name: String                    // "Braille Lite 2000 (Hungarian)"
        let file: String                    // "BL2HUN.BNS": the file a person brings
        let voice: Int                      // its voice (ssa_release_voice), or -1: the unit tab's alone
    }

    static func isRelease(_ kind: Int) -> Bool { kind >= release }

    /// Where a found firmware is kept in the unit folder, and its state beside it when the app makes one: the
    /// shipped kinds' `files` and `stateFiles`; a release voice's under the name a person brings and its id
    /// (ssa_voice_file's: BL2HUN.BNS, bl2_hu_1999_07.state); a release no voice speaks under units/, by its id,
    /// with no state (the unit tab makes its own on first start).
    static func keptAs(_ kind: Int, _ id: Identify) -> (file: String, state: String?)? {
        guard isRelease(kind) else { return files[kind].map { ($0, stateFiles[kind]) } }
        guard let r = id.release(kind - release) else { return nil }
        return r.voice >= 0 ? (r.file, r.id + ".state") : ("units/" + r.id + ".BNS", nil)
    }

    /// The unit, in words: "German Braille Lite", "Slovak Braille 'n Speak 2000", a release by the table's name, the
    /// others by their own names.
    static func name(of found: Int, _ id: Identify? = nil) -> String {
        if isRelease(found) { return id?.release(found - release)?.name ?? "Braille Lite" }
        switch found {
        case slovak: return "Slovak Braille 'n Speak 2000"
        case _ where blazie.contains(found): return "\(languageName(found)) Braille Lite"
        case speakout: return "Speak-Out"
        case mockingboard: return "Mockingboard"
        case mockingboardEarly: return "early Mockingboard"
        case accentU2, accentU3, accentU4: return "Accent SA"
        case accentMini: return "Accent-mini"
        case soundingboard: return "Sounding Board"
        case smartalk: return "SmarTalk"
        default: return "unit"
        }
    }

    static func languageName(_ found: Int) -> String {
        switch found {
        case spanish: return "Spanish"
        case german: return "German"
        case italian: return "Italian"
        case slovak: return "Slovak"
        case speakout: return "Speak-Out"
        case mockingboard: return "Mockingboard"
        case mockingboardEarly: return "early Mockingboard"
        case soundingboard: return "Sounding Board"
        case smartalk: return "SmarTalk"
        default: return "English"
        }
    }

    /// A unit's state, by its content: a size a state has (stateSizes), or one with a clock's 64 bytes after it.
    /// Never imported, whatever its name.
    static func isState(_ data: Data) -> Bool {
        if control == "state" { return false }
        if control == "state-sizes" { return data.count == stateSize }
        if stateSizes.contains(data.count) { return true }
        guard stateSizes.contains(data.count - stateTail) else { return false }
        let mark = [UInt8](data.suffix(stateTail).prefix(8))
        return stateTails.contains(mark)
    }

    /// One firmware the import will bring in.
    struct Found {
        let kind: Int                       // what it is: `english` ... `accentMini`
        let from: String                    // where it was: the file's name, or its path in the zip
        let firmware: URL                   // written into the staging folder
        let label: String                   // in words (the native side's)
        var parts: [URL] = []               // a two-file voice's other files, in `partFiles`' order (the Sounding
                                            // Board's SB.COM)
    }

    /// Either `refusal` says why nothing can be imported, in words for the person holding the device, or `found`
    /// lists what will be.  `notes` are things worth saying either way.
    struct Plan {
        let found: [Found]
        let refusal: String?
        var notes: [String] = []
    }

    /// A release kept for the unit tab alone: one no voice speaks (the Braille 'n Speak 640, Japanese, the betas).
    static func isUnitOnly(_ kind: Int, _ id: Identify) -> Bool {
        isRelease(kind) && (id.release(kind - release)?.voice ?? -1) < 0
    }

    /// What a finished import says: the voices that speak now, and the units kept for the unit tab alone said to be
    /// no voice (Tomi, 2026-10-18: a Braille 'n Speak 640 imported alone is "not a voice", never "the voice speaks
    /// now").  Android's doneMessage.
    static func doneMessage(_ found: [Found], _ id: Identify) -> String {
        let voices = found.filter { !isUnitOnly($0.kind, id) }.map { $0.label }
        let units = found.filter { isUnitOnly($0.kind, id) }.map { $0.label }
        var said: [String] = []
        if !voices.isEmpty {
            said.append("Imported \(and(voices)). " + (voices.count == 1 ? "The voice speaks now."
                                                                          : "The voices speak now."))
        }
        if !units.isEmpty {
            said.append((voices.isEmpty ? "Imported " : "Also imported ") + and(units) + " for the Blazie " +
                "Engineering unit screen. " + (units.count == 1
                    ? "Note: this one cannot be used as a voice; it runs on the unit screen."
                    : "Note: these cannot be used as voices; they run on the unit screen."))
        }
        return said.joined(separator: " ")
    }

    /// "A", "A and B", "A, B and C".
    static func and(_ items: [String]) -> String {
        items.count < 3 ? items.joined(separator: " and ")
            : items.dropLast().joined(separator: ", ") + " and " + items.last!
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

    /// Several loose files chosen together (no zips: each of those is a source of its own), judged as one source,
    /// so a voice of two files picked as its two files (the Sounding Board's SBLOAD.COM and SB.COM) is found whole.
    /// `ignored` gets the names of those that held nothing this app takes.
    static func inspect(files: [(name: String, data: Data)], staging: URL, id: Identify,
                        ignored: inout [String]) -> Plan {
        let fm = FileManager.default
        try? fm.removeItem(at: staging)
        try? fm.createDirectory(at: staging, withIntermediateDirectories: true)
        let sink = Sink(staging: staging, id: id)
        for (name, data) in files {
            if isState(data) { sink.states.append(name); continue }
            if sink.judge(from: name, bytes: data) { continue }
            let before = sink.firmware.count
            if let at = ZipReader.zipStart(data), at > 0 {      // an update program (blt2000.exe)
                try? sink.zip(name: name, data: data, at: at, nested: false)
            }
            if sink.firmware.count == before { ignored.append(name) }
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
        var unopened: [String] = []               // a zip inside a zip inside the source
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
            if (control == "speakout" && kind == speakout)
                || (control == "mockingboard" && (kind == mockingboard || kind == mockingboardEarly))
                || (control == "accent" && (accentU2...accentMini).contains(kind))
                || (control == "release" && isRelease(kind))
                || (control == "soundingboard" && (kind == sbLoad || kind == sbCom))
                || (control == "smartalk" && kind == smartalk) {
                kind = FirmwareImport.none
                text = ""
            }
            if kind == FirmwareImport.none && control != "smartalkname"
                && baseName(from).uppercased() == "S_TALK.EXE" {
                withReason.append((from, notTheSmarTalk(id.smartalkSha256())))  // another copy or version of it
            } else if kind >= 0 {
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

        /// Every entry of the zip, in whatever folders it sits (Tomi, 2026-10-18: anyone's own archive --
        /// gwmicro/sb.zip, blazie/braille-lite/BL2ENG.BNS -- imports as the add-on and the emulator's download do),
        /// each judged by its content alone, never by its name or its folder; a zip in it is opened too, one level.
        func zip(name: String, data: Data, at: Int, nested: Bool) throws {
            var zip = ZipReader(data, start: at)
            do {
                // a zip in it may be as big as the add-on (5.5 MB); nothing else that big is firmware
                while let (e, bytes) = try zip.next(cap: nested ? maxEntry : maxInner) {
                    if e.isDirectory { continue }
                    let path = e.name.replacingOccurrences(of: "\\", with: "/")
                        .trimmingCharacters(in: CharacterSet(charactersIn: "/"))
                    let parts = path.split(separator: "/").map(String.init)
                    if parts.isEmpty || parts.contains("__MACOSX") { continue }
                    if control == "1" && parts.count > 1 { continue }       // the control: the zip's top alone
                    guard let bytes = bytes else { continue }
                    let from = nested ? "\(name), \(path)" : path
                    let inner = ZipReader.isZip(bytes, at: 0)
                    if bytes.count > maxEntry && !inner { continue }
                    if isState(bytes) { states.append(from); continue }
                    // a zip is opened, not judged whole; an update program is judged, then opened when it holds no
                    // firmware itself (a zip behind its own code)
                    if inner || !judge(from: from, bytes: bytes), let s = ZipReader.zipStart(bytes) {
                        if !nested {
                            // anything an open throws means the bytes only looked like a zip (the collection's
                            // unzip.BNS, Blazie's unzip program): what it gave before is kept
                            try? self.zip(name: path, data: bytes, at: s, nested: true)
                        } else if control != "unopened" {
                            unopened.append(from)                           // the control: in silence again
                        }
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
            for kind in order + Set(firmware.map { $0.kind }.filter(isRelease)).sorted() {
                if let codes = parts[kind], let names = partFiles[kind] {
                    // a two-file voice: each file once, imported only with both
                    let have = codes.map { code in firmware.first { $0.kind == code } }
                    let got = have.compactMap { $0 }
                    if got.count == codes.count {
                        found.append(Found(kind: kind, from: got.map { $0.from }.joined(separator: " and "),
                                           firmware: got[0].file, label: soundingboardLabel,
                                           parts: got.dropFirst().map { $0.file }))
                    } else if let missing = have.firstIndex(where: { $0 == nil }) {
                        for (i, c) in have.enumerated() {
                            guard let c = c else { continue }
                            withReason.append((c.from, "the \(languageName(kind))'s \(names[i]), but its " +
                                "\(names[missing]) is needed too (both are in GW Micro's sb.zip)"))
                        }
                    }
                    continue
                }
                guard let c = firmware.filter({ $0.kind == kind }).min(by: { r($0.label) < r($1.label) }) else {
                    continue
                }
                found.append(Found(kind: kind, from: c.from, firmware: c.file, label: c.label))
            }
            let kept = Set(found.flatMap { [$0.firmware] + $0.parts })
            for c in firmware where !kept.contains(c.file) {
                try? FileManager.default.removeItem(at: c.file)
                guard let f = found.first(where: { $0.kind == c.kind }) else { continue }  // a two-file voice's part
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
            if states.count > 3 {           // the unified add-on's: one per unit and language, a wall to read
                notes.append("\(states.count) state files (\(states[0]) and others) are not firmware, and are not " +
                    "used: this device prepares each unit's state itself from its firmware.")
            } else if !states.isEmpty {
                let one = states.count == 1
                notes.append("\(states.joined(separator: ", ")) \(one ? "is a state file" : "are state files"), not " +
                    "firmware, and \(one ? "is" : "are") not used: this device prepares the unit's state itself " +
                    "from the firmware.")
            }
            // a zip inside a zip in the source, beside what was taken: said, never dropped in silence
            if !found.isEmpty && !unopened.isEmpty {
                notes.append(FirmwareImport.unopened(unopened.joined(separator: ", ")))
            }
            if !found.isEmpty { return Plan(found: found, refusal: nil, notes: notes) }

            if !states.isEmpty && unknown.isEmpty && refused.isEmpty && unopened.isEmpty && otherHex.isEmpty
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
            if !unopened.isEmpty { refusal += " " + FirmwareImport.unopened(unopened.joined(separator: ", ")) }
            return Plan(found: [], refusal: refusal + " " + whatToChoose)
        }
    }
}
