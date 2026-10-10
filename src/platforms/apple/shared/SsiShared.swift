// SsiShared.swift -- what the app and its speech extension share: the App Group and the firmware folder in it, the
// voices (Android's ssa_engine voices) and the files each needs, the settings and their defaults (Android's
// SsiSettings, the NVDA drivers' scales), and the Audio Unit's identity.  MIT.
//
// The firmware lives only in the App Group's container -- never a private folder, or the app would report an import
// the extension never sees -- with no file protection, so the voice speaks between a restart and the first unlock
// (dt-iPhone, on device: with .completeUntilFirstUserAuthentication the extension could not read the firmware then,
// its voice list came back empty, iOS cached the empty list and the voice showed as "null" until re-added).

import AudioToolbox
import Foundation

enum SsiShared {
    static let appGroup = "group.com.ssi263speech.app"

    /// The speech extension's Audio Unit: never another app's (two components with one identity once silenced
    /// VoiceOver on a whole device).
    static let componentType: OSType = 0x6175_7370        // 'ausp'
    static let componentSubType: OSType = 0x7332_3633     // 's263'
    static let componentManufacturer: OSType = 0x5353_4973  // 'SSIs'

    static var componentDescription: AudioComponentDescription {
        AudioComponentDescription(componentType: componentType, componentSubType: componentSubType,
                                  componentManufacturer: componentManufacturer, componentFlags: 0,
                                  componentFlagsMask: 0)
    }

    static var container: URL? {
        containerOverride ?? FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: appGroup)
    }

    /// The host-side tests' folder in place of the App Group's (test/test_installer.sh); never set in the apps.
    static var containerOverride: URL?

    /// The units' files, as Android's SsiData.dir: the firmware imported and the Braille Lite's states made from it.
    static var unitFolder: URL? { container?.appendingPathComponent("unit", isDirectory: true) }

    /// Where an import is judged and made ready, beside the real folder; gone when the import is.
    static var stagingFolder: URL? { container?.appendingPathComponent("unit.importing", isDirectory: true) }

    // ---- the voices -------------------------------------------------------------------------------------------------

    struct Voice {
        let index: Int32                // ssa_engine.h's SSA_ENGLISH ... SSA_MOCKINGBOARD
        let key: String                 // the system voice's identifier: com.ssi263speech.voice.<key>
        let name: String
        let language: String            // BCP 47
        let files: [String]             // in the unit folder
    }

    static let voices: [Voice] = [
        Voice(index: 0, key: "braillelite", name: "Braille Lite 2000", language: "en-US",
              files: ["BL2ENG.BNS", "bl2_2003_warm.state"]),
        Voice(index: 1, key: "braillelite-es", name: "Braille Lite 2000 (español)", language: "es-ES",
              files: ["BL2SPA.BNS", "bl2spa_fresh.state"]),
        Voice(index: 3, key: "speakout", name: "Speak-Out", language: "en-US", files: ["SPEAKOUT.HEX"]),
        Voice(index: 5, key: "mockingboard", name: "Mockingboard (Sweet Micro Systems)", language: "en-US",
              files: ["mockingboard-tts-1.1.bin"]),
        Voice(index: 2, key: "accentsa", name: "Accent SA", language: "en-US", files: ["u2.BIN", "u3.BIN", "u4.BIN"]),
        Voice(index: 4, key: "accentmini", name: "Accent-mini", language: "en-US", files: ["SPKEMS.DVC"]),
    ]

    static let identifierPrefix = "com.ssi263speech.voice."

    /// The voice a request names.  The system may hand our identifier back with the extension's bundle ID in front
    /// (macOS lists "com.ssi263speech.app.synth-extension.com.ssi263speech.voice.speakout"), so its end decides.
    static func voice(identifier: String) -> Voice? {
        voices.first { identifier == identifierPrefix + $0.key || identifier.hasSuffix("." + identifierPrefix + $0.key) }
    }

    /// The voice's files are in the unit folder.  Asks only whether they exist -- which needs no access to their
    /// contents, so a file still locked is never taken for a missing one.
    static func installed(_ v: Voice) -> Bool {
        guard let dir = unitFolder else { return false }
        return v.files.allSatisfy { FileManager.default.fileExists(atPath: dir.appendingPathComponent($0).path) }
    }

    /// What was imported, in words (written beside the firmware by the import: <file>.label).
    static func label(_ v: Voice) -> String? {
        guard let dir = unitFolder else { return nil }
        return try? String(contentsOf: dir.appendingPathComponent(v.files[0] + ".label"), encoding: .utf8)
    }

    /// No file protection on the unit folder and everything in it, put right whenever it is not (dt-iPhone's
    /// repairProtectionIfNeeded: one run of either process while unlocked fixes it for every later restart).
    static func repairProtection() {
        #if os(iOS)
        guard let dir = unitFolder else { return }
        let fm = FileManager.default
        let urls = [dir] + ((try? fm.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil)) ?? [])
        for url in urls {
            let now = (try? fm.attributesOfItem(atPath: url.path))?[.protectionKey] as? FileProtectionType
            if now != FileProtectionType.none {
                try? fm.setAttributes([.protectionKey: FileProtectionType.none], ofItemAtPath: url.path)
            }
        }
        #endif
    }

    // ---- the settings: Android's SsiSettings, on the NVDA drivers' scales ---------------------------------------

    enum Key {
        static let voice = "voice"                      // the app's preview voice
        static let rate = "rate"                        // 0-100, 50 = the unit's factory rate
        static let pitch = "pitch"                      // 0-100, 50 = the unit's factory pitch
        static let volume = "engine_volume"             // 0-200
        static let tone = "tone"                        // the Braille Lite's, 0-26
        static let shortPauses = "short_pauses"         // the Braille Lite's
        static let numbers = "numbers"                  // "Read numbers as words": the Braille Lite's, the Mockingboard's
        static let runAhead = "run_ahead"               // the Braille Lite's, EXPERIMENTAL
        static let inflection = "inflection"            // the Braille Lite's and the Accents' voice inflection
        static let soTone = "speakout_tone"             // the Speak-Out's, 0-25 = A-Z
        static let soJoin = "speakout_join"
        static let soShortPauses = "speakout_short_pauses"
        static let pauseMode = "pause_mode"             // VoiceOver's pauses: 0 off, 1 short, 2 long
        static let sampleRate = "sample_rate"           // the units': 11025 or 22050 (Android's key)
        static let logRequests = "log_requests"         // the extension logs each request's SSML (for finding bugs)
        static let firmwareStamp = "firmware_stamp"     // bumped by the app when the firmware changes
    }

    static let defaultVolume = 150                      // Android's: +3.5 dB over the desktop level, with headroom

    struct Settings {
        var rate = 50, pitch = 50, volume = SsiShared.defaultVolume, tone = 7
        var shortPauses = true, numbers = true, runAhead = false, inflection = true
        var soTone = 8, soJoin = true, soShortPauses = true
        var pauseMode = 1
        var sampleRate = SsiShared.outputRate
        var logRequests = false
    }

    /// A fresh UserDefaults each time: a long-lived one is not guaranteed to see the other process's writes
    /// promptly, which looks exactly like "the app's sliders do nothing" (dt-iPhone).
    static func defaults() -> UserDefaults? { UserDefaults(suiteName: appGroup) }

    static func load() -> Settings {
        var s = Settings()
        guard let d = defaults() else { return s }
        func int(_ k: String, _ lo: Int, _ hi: Int, _ dflt: Int) -> Int {
            d.object(forKey: k) == nil ? dflt : min(max(d.integer(forKey: k), lo), hi)
        }
        func bool(_ k: String, _ dflt: Bool) -> Bool { d.object(forKey: k) == nil ? dflt : d.bool(forKey: k) }
        s.rate = int(Key.rate, 0, 100, 50)
        s.pitch = int(Key.pitch, 0, 100, 50)
        s.volume = int(Key.volume, 0, 200, defaultVolume)
        s.tone = int(Key.tone, 0, 26, 7)
        s.shortPauses = bool(Key.shortPauses, true)
        s.numbers = bool(Key.numbers, true)
        s.runAhead = bool(Key.runAhead, false)
        s.inflection = bool(Key.inflection, true)
        s.soTone = int(Key.soTone, 0, 25, 8)
        s.soJoin = bool(Key.soJoin, true)
        s.soShortPauses = bool(Key.soShortPauses, true)
        s.pauseMode = int(Key.pauseMode, 0, 2, 1)
        s.sampleRate = sampleRates.contains(d.integer(forKey: Key.sampleRate)) ? d.integer(forKey: Key.sampleRate)
            : outputRate
        s.logRequests = bool(Key.logRequests, false)
        return s
    }

    /// The settings on ssa_engine's scales (ssa_settings).
    static func native(_ s: Settings) -> ssa_settings {
        var n = ssa_settings()
        ssa_default_settings(&n)
        n.rate = Int32(s.rate)
        n.pitch = Int32(s.pitch)
        n.volume = Int32(s.volume)
        n.tone = Int32(s.tone)
        n.pack = s.shortPauses ? 1 : 0
        n.numbers = s.numbers ? 1 : 0
        n.run_ahead = s.runAhead ? 1 : 0
        n.so_tone = Int32(s.soTone)
        n.so_join = s.soJoin ? 1 : 0
        n.so_short = s.soShortPauses ? 1 : 0
        return n
    }

    /// The Audio Unit's rate, whatever the units' (TGSpeechBox's: lower rates alias on the iPhone's DAC, 44100
    /// clicked).  The units run at 22050 Hz by default, the Audio Unit's own, so nothing is resampled; at 11025 Hz
    /// (Android's "11 kHz, like the unit's own speaker") their PCM is the desktop's at that rate, brought up to the
    /// Audio Unit's by one converter per utterance.  Nothing above it: 44100 Hz would only be brought down again.
    static let outputRate = 22050
    static let sampleRates = [11025, 22050]

    /// A Swift string's bytes as ssa_engine takes them (NUL-terminated UTF-8).
    static func withUTF8<T>(_ s: String, _ body: (UnsafePointer<CChar>) -> T) -> T {
        s.withCString(body)
    }
}

/// One engine over the unit folder: Android's ssa_engine with the Accents' files handed over in memory, as the
/// Android app hands over its built-in ones.  Not thread-safe: its owner serialises every call (ssp_speech_stop
/// aside).
final class SsiEngine {
    let engine: OpaquePointer
    private var aicomLoaded = false
    private var miniLoaded = false

    init?() {
        guard let dir = SsiShared.unitFolder, let e = ssa_new(dir.path) else { return nil }
        engine = e
        ssa_configure(e, Int32(SsiShared.outputRate), 1, 0)
    }

    deinit { ssa_free(engine) }

    /// The Accents' files from the unit folder, read once they are all there (an import may bring them later).
    func loadAicom() {
        guard let dir = SsiShared.unitFolder else { return }
        if !aicomLoaded {
            let roms = ["u2.BIN", "u3.BIN", "u4.BIN"].compactMap { try? Data(contentsOf: dir.appendingPathComponent($0)) }
            if roms.count == 3 {
                aicomLoaded = roms[0].withUnsafeBytes { a in roms[1].withUnsafeBytes { b in roms[2].withUnsafeBytes { c in
                    ssa_set_accent_roms(engine, a.bindMemory(to: UInt8.self).baseAddress, roms[0].count,
                                        b.bindMemory(to: UInt8.self).baseAddress, roms[1].count,
                                        c.bindMemory(to: UInt8.self).baseAddress, roms[2].count) == 1
                }}}
            }
        }
        if !miniLoaded, let dvc = try? Data(contentsOf: dir.appendingPathComponent("SPKEMS.DVC")) {
            miniLoaded = dvc.withUnsafeBytes {
                ssa_set_accent_mini(engine, $0.bindMemory(to: UInt8.self).baseAddress, dvc.count) == 1
            }
        }
    }

    /// Forget the Accents' files (after a removal or a new import), so the next use reads them again.
    func forgetAicom() {
        aicomLoaded = false
        miniLoaded = false
    }

    /// The boot settings: a change shuts the units down (the next use boots them again).
    func configure(sampleRate: Int, inflection: Bool) {
        ssa_configure(engine, Int32(sampleRate), inflection ? 1 : 0, 0)
    }

    var sampleRate: Int { Int(ssa_sample_rate(engine)) }

    func has(_ v: SsiShared.Voice) -> Bool { ssa_has_voice(engine, v.index) != 0 }
}
