// FirmwareInstaller.swift -- an import on the device, Android's SsiImport.kt: a source's bytes judged by
// FirmwareImport with the native side's eyes (ssp_import.c), then brought in -- each Braille Lite unit's state made
// here from its firmware (bl_state.c) and checked against the list's, the Speak-Out's HEX and Aicom's files taken as
// they are -- each Braille Lite and Speak-Out unit made to speak once, and only then everything moved into the unit
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

    func known() -> [String] {
        (0..<Int(blv_firmware_count())).compactMap { blv_firmware_label(Int32($0)).map { String(cString: $0) } }
    }
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
        for (i, url) in urls.enumerated() {
            let data = try read(url)
            let plan = FirmwareImport.inspect(name: url.lastPathComponent, data: data,
                                              staging: staging.appendingPathComponent("source\(i)", isDirectory: true),
                                              id: NativeIdentify())
            plans.append((url.lastPathComponent, plan))
        }
        if plans.count == 1 { return plans[0].1 }
        var found: [FirmwareImport.Found] = []
        for kind in FirmwareImport.order {
            if let f = plans.lazy.compactMap({ $0.1.found.first { $0.kind == kind } }).first { found.append(f) }
        }
        let notes = plans.flatMap { $0.1.notes }
        if found.isEmpty {
            return FirmwareImport.Plan(found: [], refusal: plans.compactMap { p in p.1.refusal.map { "\(p.0): \($0)" } }
                                       .joined(separator: " "))
        }
        let empty = plans.filter { $0.1.found.isEmpty }.map { "\($0.0) holds no firmware this app takes; it is left out." }
        return FirmwareImport.Plan(found: found, refusal: nil, notes: notes + empty)
    }

    /// The probe each unit speaks before it is taken.
    private static func probeText(_ kind: Int) -> String { kind == FirmwareImport.spanish ? "Hola." : "Hello." }

    /// English's state is 150 million Z180 instructions, Spanish's 1150 million; the rest only a check.
    private static func weight(_ kind: Int) -> Double {
        kind == FirmwareImport.spanish ? 1150 : kind == FirmwareImport.english ? 150 : 15
    }

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
            let fm = FileManager.default
            let ready = staging.appendingPathComponent("ready", isDirectory: true)
            try? fm.removeItem(at: ready)
            try fm.createDirectory(at: ready, withIntermediateDirectories: true)
            defer { try? fm.removeItem(at: staging) }
            var labels: [String] = []
            for f in plan.found {
                let name = FirmwareImport.name(of: f.kind)
                let file = FirmwareImport.files[f.kind]!
                let target = ready.appendingPathComponent(file)
                try? fm.removeItem(at: target)
                try fm.moveItem(at: f.firmware, to: target)
                let w = weight(f.kind)
                if let stateFile = FirmwareImport.stateFiles[f.kind] {
                    say("Preparing the \(name) unit")
                    let state = ready.appendingPathComponent(stateFile)
                    var err = [CChar](repeating: 0, count: 256)
                    let me = Unmanaged.passUnretained(self).toOpaque()
                    making = w
                    let ok = blv_make_state(target.path, Int32(f.kind), state.path, { ctx, done in
                        let job = Unmanaged<Job>.fromOpaque(ctx!).takeUnretainedValue()
                        job.advance(done)
                        return job.cancelled ? 1 : 0
                    }, me, &err, Int32(err.count)) == 1
                    making = 0
                    if cancelled { throw CancellationError() }
                    if !ok { throw Failure(message: "The \(name) unit could not be prepared: \(String(cString: err)).") }
                    // Each release on the list makes the state it was tested with, byte for byte.
                    if blv_state_check(target.path, state.path) != 1 {
                        throw Failure(message: "The \(name) unit prepared on this device is not the one the voice " +
                                      "was tested with, so it was not imported.")
                    }
                }
                if f.kind == FirmwareImport.english || f.kind == FirmwareImport.spanish
                    || f.kind == FirmwareImport.speakout {
                    say("Checking that the \(name) unit speaks")
                    var err = [CChar](repeating: 0, count: 256)
                    let voice: Int32 = f.kind == FirmwareImport.speakout ? 3 : Int32(f.kind)
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
            try fm.createDirectory(at: unit, withIntermediateDirectories: true)
            for item in try fm.contentsOfDirectory(at: ready, includingPropertiesForKeys: nil) {
                let dest = unit.appendingPathComponent(item.lastPathComponent)
                try? fm.removeItem(at: dest)
                try fm.moveItem(at: item, to: dest)
            }
            SsiShared.repairProtection()
            return labels
        }
    }

    /// Every imported file gone.
    static func removeAll() {
        if let unit = SsiShared.unitFolder { try? FileManager.default.removeItem(at: unit) }
        if let staging = SsiShared.stagingFolder { try? FileManager.default.removeItem(at: staging) }
    }
}
