// FirmwareInstaller.swift -- an import on the device, Android's SsiImport.kt: a source's bytes judged by
// FirmwareImport with the native side's eyes (ssp_import.c), then brought in -- each Braille Lite unit's state made
// here from its firmware (bl_state.c) and checked against the list's, the Speak-Out's HEX and Aicom's files taken as
// they are, the Mockingboard's file as the native side made it (from the toolkit disk too) -- each Braille Lite,
// Speak-Out and Mockingboard unit made to speak once, and only then everything moved into the unit
// folder together.  Also the removal of every imported file.  MIT.

import Foundation

/// The native side's judgement, for FirmwareImport.
struct NativeIdentify: FirmwareImport.Identify {
    func firmware(_ data: Data, out: URL) -> (Int, String) {
        var msg = [CChar](repeating: 0, count: 512)
        let r = data.withUnsafeBytes { b in
            ssp_import_firmware(b.bindMemory(to: UInt8.self).baseAddress, data.count, out.path, &msg, Int32(msg.count))
        }
        return (Int(r), String(cString: msg))
    }

    // only the releases with a voice: the emulator's own are never offered or named to the voices
    func known() -> [String] {
        (0..<Int(blv_firmware_count())).filter { blv_firmware_language(Int32($0)) >= 0 }
            .compactMap { blv_firmware_label(Int32($0)).map { String(cString: $0) } }
    }

    func release(_ row: Int) -> FirmwareImport.Release? {
        let k = Int32(row)
        guard let id = blv_release_id(k), let name = blv_release_name(k), let file = blv_release_file(k) else {
            return nil
        }
        return FirmwareImport.Release(id: String(cString: id), name: String(cString: name),
                                      file: String(cString: file), voice: Int(ssa_release_voice(k)))
    }

    func smartalkSha256() -> String { String(cString: ssp_smartalk_sha256()) }
}

enum FirmwareInstaller {
    struct Failure: LocalizedError {
        let message: String
        var errorDescription: String? { message }
    }

    /// A file the person chose: read whole (firmware is a few hundred kilobytes, an add-on a few megabytes), inside
    /// its security scope.
    static func read(_ url: URL) throws -> Data {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        let size = (try? url.resourceValues(forKeys: [.fileSizeKey]))?.fileSize ?? 0
        if size > FirmwareImport.maxSource {
            throw Failure(message: "\(url.lastPathComponent) is larger than 64 MB, so it is not firmware, an update " +
                          "for it or a package containing them.")
        }
        return try Data(contentsOf: url)
    }

    /// Every file the person chose, each judged into a staging folder of its own, as one plan: each kind of
    /// firmware from the first file that holds it, every note, and each file that held none said in a note -- or,
    /// when none held any, every refusal.
    static func inspect(_ urls: [URL]) throws -> FirmwareImport.Plan {
        guard let staging = SsiShared.stagingFolder else {
            throw Failure(message: "The app's shared folder is not available, so nothing can be imported.")
        }
        try? FileManager.default.removeItem(at: staging)
        var plans: [(String, FirmwareImport.Plan)] = []
        var loose: [(name: String, data: Data)] = []    // several files chosen: the ones that are not zips, together
        for (i, url) in urls.enumerated() {
            let data = try read(url)
            if urls.count > 1 && !ZipReader.isZip(data, at: 0) {
                loose.append((url.lastPathComponent, data))
                continue
            }
            let plan = FirmwareImport.inspect(name: url.lastPathComponent, data: data,
                                              staging: staging.appendingPathComponent("source\(i)", isDirectory: true),
                                              id: NativeIdentify())
            plans.append((url.lastPathComponent, plan))
        }
        var ignored: [String] = []
        if !loose.isEmpty {
            // judged as one source, so a voice of two files (the Sounding Board's drivers) chosen as its two files is
            // found whole
            let plan = FirmwareImport.inspect(files: loose, staging: staging.appendingPathComponent("loose",
                                              isDirectory: true), id: NativeIdentify(), ignored: &ignored)
            plans.append((loose.map { $0.name }.joined(separator: ", "), plan))
        }
        if plans.count == 1 && loose.isEmpty { return plans[0].1 }
        var found: [FirmwareImport.Found] = []
        let releases = Set(plans.flatMap { $0.1.found.map { $0.kind } }.filter(FirmwareImport.isRelease)).sorted()
        for kind in FirmwareImport.order + releases {
            if let f = plans.lazy.compactMap({ $0.1.found.first { $0.kind == kind } }).first { found.append(f) }
        }
        let notes = plans.flatMap { $0.1.notes }
        if found.isEmpty {
            return FirmwareImport.Plan(found: [], refusal: plans.compactMap { p in p.1.refusal.map { "\(p.0): \($0)" } }
                                       .joined(separator: " "))
        }
        // each source that held nothing, the loose files by name (their plan's own notes say why a part was left out)
        let empty = (plans.prefix(plans.count - (loose.isEmpty ? 0 : 1)).filter { $0.1.found.isEmpty }.map { $0.0 }
                     + ignored)
            .map { "\($0) holds no firmware this app takes; it is left out." }
        return FirmwareImport.Plan(found: found, refusal: nil, notes: notes + empty)
    }

    /// The probe each unit speaks before it is taken, in its own language (Android's SsiImport.probeText).
    private static func probeText(_ kind: Int) -> String {
        switch kind {
        case FirmwareImport.spanish: return "Hola."
        case FirmwareImport.german: return "Hallo."
        case FirmwareImport.italian: return "Ciao."
        case FirmwareImport.slovak: return "Ahoj."
        default: return "Hello."
        }
    }

    /// Spanish's state is 1150 million Z180 instructions, every other Blazie unit's English's 150 million (German,
    /// Italian and Slovak run English's recipe or one as long; a release voice's takes as long as English's, about
    /// 3 s on an M-series Mac, as Android's SsiImport weighs it); the rest only a check.
    private static func weight(_ kind: Int) -> Double {
        // (the Sounding Board, the SmarTalk and the SynPhonix, as the Speak-Out: checked, nothing prepared)
        kind == FirmwareImport.spanish ? 1150 : FirmwareImport.blazie.contains(kind) || isReleaseVoice(kind) ? 150 : 15
    }

    /// The confirmation's words, Android's: a line per unit -- what it is and where from, how long preparing it
    /// takes, and whether it replaces one already here -- then the notes.
    static func confirmation(_ plan: FirmwareImport.Plan) -> String {
        let id = NativeIdentify(), dir = SsiShared.unitFolder
        let lines = plan.found.map { f -> String in
            var s = "Will import \(f.label) (from \(f.from)). "
            let kept = FirmwareImport.keptAs(f.kind, id)
            if f.kind == FirmwareImport.spanish {
                s += "The unit is then prepared on this device, which takes about a minute."
            } else if FirmwareImport.blazie.contains(f.kind) || isReleaseVoice(f.kind) {
                s += "The unit is then prepared on this device, which takes a few seconds."
            } else if FirmwareImport.isRelease(f.kind) {
                s += "It is for the Blazie Engineering unit tab alone: no voice speaks it yet, so it is kept for " +
                    "the unit tab, which prepares it on its first start."
            } else {
                s += "It is then checked on this device, which takes a moment."
            }
            if let file = kept?.file, let d = dir, FileManager.default.fileExists(atPath: d.appendingPathComponent(file).path) {
                s += " The \(FirmwareImport.name(of: f.kind, id)) firmware already here will be replaced."
            }
            return s
        }
        return (lines + plan.notes).joined(separator: "\n\n")
    }

    /// Android's floor: below 16 MB free an import is refused before anything is written.
    static func checkRoom(_ folder: URL) throws {
        guard let free = try? folder.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
                .volumeAvailableCapacityForImportantUsage, free < 16 << 20 else { return }
        throw Failure(message: "Not enough room: the firmware and the unit's state need about 3 MB, and this device " +
                      "has \(max(free, 0) >> 10) KB free.")
    }

    /// A release voice found (Hungarian ...): its ssa_engine voice, else nil.
    private static func releaseVoice(_ kind: Int) -> Int32? {
        guard FirmwareImport.isRelease(kind) else { return nil }
        let v = ssa_release_voice(Int32(kind - FirmwareImport.release))
        return v >= 0 ? v : nil
    }

    private static func isReleaseVoice(_ kind: Int) -> Bool { releaseVoice(kind) != nil }

    /// The units heard once before they are taken: the Blazie units, the Speak-Out, the Sounding Board, the SmarTalk,
    /// the SynPhonix (known by its content, never a hash: each copy holds its own card's serial) and the Mockingboards
    /// (the Accents' files are known by their sha256 alone, and the Accent-PC needs all three).
    private static let probed: Set<Int> = FirmwareImport.blazie.union([FirmwareImport.speakout,
                                                                      FirmwareImport.soundingboard,
                                                                      FirmwareImport.smartalk,
                                                                      FirmwareImport.synphonix,
                                                                      FirmwareImport.mockingboard,
                                                                      FirmwareImport.mockingboardEarly])

    /// An import in flight: its progress (0...1) and step in words, and its cancel.
    final class Job: @unchecked Sendable {
        let plan: FirmwareImport.Plan
        private(set) var step = ""
        private(set) var progress = 0.0
        private var made = 0.0
        private var making = 0.0           // the weight of the state being made now
        private let total: Double
        fileprivate var cancelled = false
        var onChange: ((Job) -> Void)?

        init(plan: FirmwareImport.Plan) {
            self.plan = plan
            total = max(1, plan.found.map { weight($0.kind) }.reduce(0, +))
        }

        func cancel() { cancelled = true }

        fileprivate func say(_ s: String) {
            step = s
            onChange?(self)
        }

        /// blv_make_state's progress through the state being made (0...1), reported every 10 million instructions.
        fileprivate func advance(_ partOfCurrent: Double) {
            progress = min(1, (made + partOfCurrent * making) / total)
            onChange?(self)
        }

        fileprivate func done(weight w: Double) {
            made += w
            progress = min(1, made / total)
            onChange?(self)
        }

        /// Off the main thread: each unit made ready in the staging folder, then all of them moved into place.  The
        /// labels of what was imported.
        func commit() throws -> [String] {
            guard let staging = SsiShared.stagingFolder, let unit = SsiShared.unitFolder else {
                throw Failure(message: "The app's shared folder is not available.")
            }
            try FirmwareInstaller.checkRoom(unit)
            let fm = FileManager.default
            let ready = staging.appendingPathComponent("ready", isDirectory: true)
            try? fm.removeItem(at: ready)
            try fm.createDirectory(at: ready, withIntermediateDirectories: true)
            defer { try? fm.removeItem(at: staging) }
            var labels: [String] = []
            let id = NativeIdentify()
            for f in plan.found {
                let name = FirmwareImport.name(of: f.kind, id)
                guard let (file, stateFile) = FirmwareImport.keptAs(f.kind, id) else {
                    throw Failure(message: "\(name) has no place in this app.")
                }
                let target = ready.appendingPathComponent(file)
                try fm.createDirectory(at: target.deletingLastPathComponent(), withIntermediateDirectories: true)
                try? fm.removeItem(at: target)
                try fm.moveItem(at: f.firmware, to: target)
                for (i, part) in f.parts.enumerated() {    // a two-file voice's others (the Sounding Board's SB.COM)
                    guard let names = FirmwareImport.partFiles[f.kind], i + 1 < names.count else {
                        throw Failure(message: "\(name) has no place in this app.")
                    }
                    let dest = ready.appendingPathComponent(names[i + 1])
                    try? fm.removeItem(at: dest)
                    try fm.moveItem(at: part, to: dest)
                }
                let w = weight(f.kind)
                if let stateFile = stateFile {
                    say("Preparing the \(name) unit")
                    let state = ready.appendingPathComponent(stateFile)
                    var err = [CChar](repeating: 0, count: 256)
                    let me = Unmanaged.passUnretained(self).toOpaque()
                    making = w
                    let progress: blv_state_progress = { ctx, done in
                        let job = Unmanaged<Job>.fromOpaque(ctx!).takeUnretainedValue()
                        job.advance(done)
                        return job.cancelled ? 1 : 0
                    }
                    let ok: Bool
                    if FirmwareImport.isRelease(f.kind) {
                        // a release voice's: its own recipe, the table's (blv_make_release_state)
                        ok = blv_make_release_state(target.path, Int32(f.kind - FirmwareImport.release), state.path,
                                                    progress, me, &err, Int32(err.count)) == 1
                    } else {
                        // the state's recipe, from the voice (ssa_blazie_recipe: a language is neither a voice index
                        // nor a recipe -- the Slovak unit runs the English one)
                        let recipe = ssa_blazie_recipe(Int32(f.kind))
                        if recipe < 0 { throw Failure(message: "The \(name) unit has no way to be prepared.") }
                        ok = blv_make_state(target.path, recipe, state.path, progress, me, &err, Int32(err.count)) == 1
                    }
                    making = 0
                    if cancelled { throw CancellationError() }
                    if !ok { throw Failure(message: "The \(name) unit could not be prepared: \(String(cString: err)).") }
                    // Each release on the list makes the state it was tested with, byte for byte.
                    if blv_state_check(target.path, state.path) != 1 {
                        throw Failure(message: "The \(name) unit prepared on this device is not the one the voice " +
                                      "was tested with, so it was not imported.")
                    }
                }
                if probed.contains(f.kind) || isReleaseVoice(f.kind) {
                    say("Checking that the \(name) unit speaks")
                    var err = [CChar](repeating: 0, count: 256)
                    // the found kind is ssa_engine's voice for every probed shipped unit; a release voice's its own
                    let voice = releaseVoice(f.kind) ?? Int32(f.kind)
                    let samples = ssa_probe(ready.path, voice, probeText(f.kind), nil, &err, Int32(err.count))
                    if samples < 0 {
                        throw Failure(message: "The \(name) unit would not start: \(String(cString: err)).")
                    }
                    if samples == 0 {
                        throw Failure(message: "The \(name) unit started but stayed silent, so this firmware was " +
                                      "not imported.")
                    }
                }
                try f.label.write(to: ready.appendingPathComponent(file + ".label"), atomically: true, encoding: .utf8)
                labels.append(f.label)
                done(weight: w)
                if cancelled { throw CancellationError() }
            }
            say("Moving the firmware into place")
            try move(ready, into: unit)
            SsiShared.repairProtection()
            return labels
        }
    }

    /// Every file in `from` moved into `to`, a folder's files one by one (units/ keeps the releases imported before).
    private static func move(_ from: URL, into to: URL) throws {
        let fm = FileManager.default
        try fm.createDirectory(at: to, withIntermediateDirectories: true)
        for item in try fm.contentsOfDirectory(at: from, includingPropertiesForKeys: [.isDirectoryKey]) {
            let dest = to.appendingPathComponent(item.lastPathComponent)
            if (try? item.resourceValues(forKeys: [.isDirectoryKey]))?.isDirectory == true {
                try move(item, into: dest)
                continue
            }
            try? fm.removeItem(at: dest)
            try fm.moveItem(at: item, to: dest)
        }
    }

    /// Every imported file gone.
    static func removeAll() {
        if let unit = SsiShared.unitFolder { try? FileManager.default.removeItem(at: unit) }
        if let staging = SsiShared.stagingFolder { try? FileManager.default.removeItem(at: staging) }
    }
}
