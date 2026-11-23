// UnitCatalog.swift -- the Braille Lite unit tab's units: what the emulator's catalog (emu_catalog.c's emc_list, the
// one every emulator shell reads) finds among the firmware the person imported -- the shipped units under their flat
// names, then every release on the table by its content, units/ included -- and nothing more: no firmware, no unit
// (Tomi).  Each unit's memory is kept in the app's own folder, never the App Group's, whose states are the voices'
// and are only ever read here.  A unit's first start: its saved memory, else its factory state beside its firmware,
// else (a release unit, which has none) one made from its firmware by its own recipe into its memory's place.  MIT.

import Foundation

enum UnitCatalog {
    /// A unit the folder can run: emc_list's, and its words.
    struct Entry: Identifiable, Hashable {
        let unit: emc_unit
        var name: String { String(cString: unit.name) }
        var key: String { String(cString: unit.key) }
        var id: String { key }
        static func == (a: Entry, b: Entry) -> Bool { a.key == b.key }
        func hash(into h: inout Hasher) { h.combine(key) }
    }

    /// The tests' folder in place of the app's own (test/InstallerTests.swift); never set in the app.
    static var savedOverride: URL?

    /// Where each unit's memory is kept: the app's own Application Support, "Braille Lite units".
    static var savedFolder: URL? {
        savedOverride ?? FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first?
            .appendingPathComponent("Braille Lite units", isDirectory: true)
    }

    /// The tests' controls (SSI263_UNIT_BREAK; never set in the app): `factory`, a release unit's first start skips
    /// making its state, as if it shipped one -- so its start must fail; `restart`, the tab's sound is left stopped
    /// when the system stops it (UnitRunner, #60).
    static var control = ProcessInfo.processInfo.environment["SSI263_UNIT_BREAK"] ?? ""

    /// The units whose firmware is in the unit folder, in the catalog's order.
    static func list() -> [Entry] {
        guard let dir = SsiShared.unitFolder else { return [] }
        var units = [emc_unit](repeating: emc_unit(), count: Int(EMC_MAX))
        let n = Int(emc_list(dir.path, &units, EMC_MAX))
        return units.prefix(n).filter { $0.present != 0 }.map { Entry(unit: $0) }
    }

    /// The file the unit's memory is saved in.
    static func savedPath(_ e: Entry) -> URL? {
        var u = e.unit
        let name = withUnsafeBytes(of: &u.saved) { String(cString: $0.bindMemory(to: CChar.self).baseAddress!) }
        return savedFolder?.appendingPathComponent(name)
    }

    struct Failure: LocalizedError {
        let message: String
        var errorDescription: String? { message }
    }

    /// The state the unit starts from (its saved memory, or its factory state, made first for a release unit), or
    /// nil for a cold start (a Type 'n Speak's).  A release unit's first start takes a few seconds.
    static func prepare(_ e: Entry) throws -> String? {
        guard let saved = savedPath(e), let folder = savedFolder else {
            throw Failure(message: "The app's own folder is not available.")
        }
        let fm = FileManager.default
        if fm.fileExists(atPath: saved.path) { return saved.path }
        var u = e.unit
        if emc_needs_factory(&u) != 0 {
            if control == "factory" { return saved.path }
            try fm.createDirectory(at: folder, withIntermediateDirectories: true)
            var err = [CChar](repeating: 0, count: 300)
            if emc_make_factory(&u, saved.path, nil, nil, &err, Int32(err.count)) != 1 {
                throw Failure(message: "The \(e.name) could not be set up for its first start: " +
                              String(cString: err) + ".")
            }
            return saved.path
        }
        let factory = withUnsafeBytes(of: &u.factory) { String(cString: $0.bindMemory(to: CChar.self).baseAddress!) }
        return factory.isEmpty ? nil : factory
    }

    /// The unit's firmware file.
    static func firmware(_ e: Entry) -> String {
        var u = e.unit
        return withUnsafeBytes(of: &u.firmware) { String(cString: $0.bindMemory(to: CChar.self).baseAddress!) }
    }
}
