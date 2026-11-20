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
        let index: Int32                // ssa_engine.h's SSA_ENGLISH ... SSA_SLOVAK, then the release voices'
        let key: String                 // the system voice's identifier: com.ssi263speech.<key>.<bcp47>
        let name: String                // the app's name for it (voices.h's)
        let short: String               // the system's, before the locale: "Braille Lite 2000 (de-DE)"
        let locales: [String]           // BCP 47: the first the voice's own, the rest regional variants it is offered as
        let files: [String]             // in the unit folder (ssa_engine.c's names)
        let bring: String               // the file a person is expected to bring, for the firmware list
        var release: Int32 = -1         // a release voice's row in the release table (blv_release_*), else -1
        var language: String { locales[0] }
        /// The firmware list's name: a release voice's in English, as the table names its release ("Braille Lite
        /// 2000 (Hungarian)"), so the list reads in one language; every other voice's own.
        var listName: String { release >= 0 ? blv_release_name(release).map { String(cString: $0) } ?? name : name }
    }

    /// One locale per voice, as Android offers them: the units have no dialect firmware (the English ones are
    /// American, the Spanish Braille Lite Spain's -- it reads numbers Spain's way), so no regional copies (Tomi;
    /// 0.8's first builds offered TGSpeechBox's en-GB, en-AU, en-CA and es-MX too).  An identifier of those still
    /// names its voice (voice(identifier:)).
    private static let english = ["en-US"]
    private static let spanish = ["es-ES"]

    /// Every voice the app knows, in the order the app lists them: the shipped voices, the release voices, then the
    /// voices after them (0.8.5) -- but the Sounding Board right after the Speak-Out, GW Micro's two sharing their
    /// front end, as the NVDA driver lists them (Tomi, 2026-10-25). The indexes, and so every saved voice, stay
    /// ssa_engine.h's.
    static let voices: [Voice] = listed(shipped + releaseVoices() + afterReleases)

    private static func listed(_ all: [Voice]) -> [Voice] {
        var list = all.filter { $0.key != "soundingboard" }
        if let sb = all.first(where: { $0.key == "soundingboard" }),
           let so = list.firstIndex(where: { $0.key == "speakout" }) {
            list.insert(sb, at: so + 1)
        }
        return list
    }

    private static let shipped: [Voice] = [
        Voice(index: 0, key: "braillelite", name: "Braille Lite 2000", short: "Braille Lite 2000", locales: english,
              files: ["BL2ENG.BNS", "bl2_2003_warm.state"], bring: "BL2ENG.BNS"),
        Voice(index: 1, key: "braillelite-es", name: "Braille Lite 2000 (Spanish)", short: "Braille Lite 2000",
              locales: spanish, files: ["BL2SPA.BNS", "bl2spa_fresh.state"], bring: "BL2SPA.BNS"),
        Voice(index: 7, key: "braillelite-de", name: "Braille Lite 2000 (German)", short: "Braille Lite 2000",
              locales: ["de-DE"], files: ["Lite2000_de.BNS", "lite2000_de_fresh.state"], bring: "Lite2000.BNS"),
        Voice(index: 8, key: "braillelite-it", name: "Braille Lite 2000 (Italian)", short: "Braille Lite 2000",
              locales: ["it-IT"], files: ["Lite2000_it.BNS", "lite2000_it_fresh.state"], bring: "Lite2000.BNS"),
        Voice(index: 9, key: "braillenspeak-sk", name: "Braille 'n Speak 2000 (Slovak)",
              short: "Braille 'n Speak 2000", locales: ["sk-SK"], files: ["BS2SLL.BNS", "bs2sll_fresh.state"],
              bring: "BS2SLL.BNS"),
        Voice(index: 3, key: "speakout", name: "Speak-Out", short: "Speak-Out", locales: english,
              files: ["SPEAKOUT.HEX"], bring: "SPEAKOUT.HEX"),
        Voice(index: 5, key: "mockingboard", name: "Mockingboard (Sweet Micro Systems)", short: "Mockingboard",
              locales: english, files: ["mockingboard-tts-1.1.bin"], bring: "the Developers Toolkit disk"),
        Voice(index: 6, key: "mockingboard-early", name: "Mockingboard, early (Sweet Micro Systems)",
              short: "Mockingboard, early", locales: english, files: ["mockingboard-tts-early.bin"],
              bring: "Mockingboard disk 1"),
        Voice(index: 2, key: "accentsa", name: "Accent-PC", short: "Accent-PC", locales: english,
              files: ["u2.BIN", "u3.BIN", "u4.BIN"], bring: "u2.BIN, u3.BIN and u4.BIN"),
        Voice(index: 4, key: "accentmini", name: "Accent-mini", short: "Accent-mini", locales: english,
              files: ["SPKEMS.DVC"], bring: "SPKEMS.DVC"),
    ]

    /// The voices after the release table's (ssa_engine.h, 0.8.5: SSA_SOUNDINGBOARD is SSA_VOICES, so no voice
    /// before them moves): GW Micro's Sounding Board, the Speak-Out's voice on the card's own DOS drivers, imported from
    /// sb.zip (its two files: ssa_engine.c's AFTER_RELEASES); then Automated Functions' SmarTalk (0.9.0,
    /// SSA_SMARTALK), its own DOS text-to-speech S_TALK.EXE on the same emulated PC, imported, never in the app.
    private static let afterReleases: [Voice] = [
        Voice(index: 27, key: "soundingboard", name: "Sounding Board (GW Micro)", short: "Sounding Board",
              locales: english, files: ["SBLOAD.COM", "SB.COM"], bring: "GW Micro's sb.zip (SBLOAD.COM and SB.COM)"),
        Voice(index: 28, key: "smartalk", name: "SmarTalk (Automated Functions)", short: "SmarTalk",
              locales: english, files: ["S_TALK.EXE"], bring: "S_TALK.EXE 1.21 (36,882 bytes)"),
    ]

    /// The release voices, from the release table the library carries (bl_languages.txt; never a list kept here):
    /// one per row that names a new voice, its files and number ssa_engine's (ssa_release_voice, ssa_voice_file).
    private static func releaseVoices() -> [Voice] {
        (0..<blv_firmware_count()).compactMap { k -> Voice? in
            let index = ssa_release_voice(k)
            guard index >= 0, let key = blv_release_voice_key(k), let name = blv_release_voice_name(k),
                  let tag = blv_release_tag(k), let file = blv_release_file(k) else { return nil }
            var files: [String] = []
            var buf = [CChar](repeating: 0, count: 256)
            while ssa_voice_file(index, Int32(files.count), &buf, Int32(buf.count)) == 1 {
                files.append(String(cString: buf))
            }
            // blazie_pt_br -> braillelite-pt-br, beside the shipped braillelite-de
            let own = String(cString: key).dropFirst("blazie_".count).replacingOccurrences(of: "_", with: "-")
            return Voice(index: index, key: "braillelite-" + own, name: String(cString: name),
                         short: "Braille Lite 2000", locales: [systemLocale(String(cString: tag))], files: files,
                         bring: String(cString: file), release: k)
        }
    }

    /// A release's language (BCP 47) as the system names its voices' languages: a region given stays (pt-BR,
    /// ar-AE); else the language's likeliest one (hu-HU, el-GR), except where the system speaks otherwise:
    /// Arabic is ar-001 and Norwegian nb-NO, and plain Portuguese is Portugal's (Brazil's has its own release).
    static func systemLocale(_ tag: String) -> String {
        if tag.contains("-") { return tag }
        switch tag {
        case "ar": return "ar-001"
        case "no", "nb": return "nb-NO"
        case "pt": return "pt-PT"
        default:
            let region = Locale.Language(identifier: Locale.Language(identifier: tag).maximalIdentifier).region
            return region.map { "\(tag)-\($0.identifier)" } ?? tag
        }
    }

    /// The system voice's identifier, TGSpeechBox's form: the voice, then the locale it is offered as.
    static let identifierRoot = "com.ssi263speech."
    static func identifier(_ v: Voice, _ locale: String) -> String { identifierRoot + v.key + "." + locale.lowercased() }

    /// The voice and locale a request names, read from its identifier as TGSpeechBox reads its own: the last
    /// "com.ssi263speech." in it (macOS may put the extension's bundle ID in front), then <key>.<bcp47>.  The 0.8
    /// betas' com.ssi263speech.voice.<key> still names its voice, in its own locale.
    static func voice(identifier: String) -> (voice: Voice, locale: String)? {
        guard let r = identifier.range(of: identifierRoot, options: .backwards) else { return nil }
        let rest = String(identifier[r.upperBound...])
        if rest.hasPrefix("voice.") {
            let key = String(rest.dropFirst(6))
            return voices.first { $0.key == key }.map { ($0, $0.language) }
        }
        guard let dot = rest.lastIndex(of: ".") else { return nil }
        let key = String(rest[..<dot]), locale = String(rest[rest.index(after: dot)...])
        guard let v = voices.first(where: { $0.key == key }) else { return nil }
        return (v, v.locales.first { $0.lowercased() == locale.lowercased() } ?? v.language)
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
        let urls = [dir] + (fm.enumerator(at: dir, includingPropertiesForKeys: nil)?.allObjects as? [URL] ?? [])
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
        static let lineLift = "line_lift"               // the Braille Lite's "Lift line starts (as note-taking mode)"
        static let inflection = "inflection"            // the Braille Lite's and the Accents' voice inflection
        static let soTone = "speakout_tone"             // the Speak-Out's, 0-25 = A-Z
        static let soJoin = "speakout_join"
        static let soShortPauses = "speakout_short_pauses"
        static let whine = "whine"                      // the Blazie units' idle sound: 0 off, 1 hiss, 2 whine
        static let pauseMode = "pause_mode"             // VoiceOver's pauses: 0 off, 1 short, 2 long
        static let sampleRate = "sample_rate"           // the units': 11025 or 22050 (Android's key)
        static let logRequests = "log_requests"         // the extension logs each request's SSML (for finding bugs)
        static let lockLanguage = "lock_language"       // TGSpeechBox's: every request by the app's chosen voice
        static let firmwareStamp = "firmware_stamp"     // bumped by the app when the firmware changes
    }

    // Android's: +2.6 dB over the desktop level (100).  150 until engine v0.16's F1 lifted every voice's peaks about
    // 2 dB and the Speak-Out clipped; at 135 the loudest of test_volume_headroom.py's lines is at -0.86 dBFS
    static let defaultVolume = 135

    struct Settings {
        var rate = 50, pitch = 50, volume = SsiShared.defaultVolume, tone = 7
        var shortPauses = true, numbers = true, runAhead = false, lineLift = false, inflection = true
        var soTone = 8, soJoin = true, soShortPauses = true
        var pauseMode = 1
        var whine = 0
        var sampleRate = SsiShared.outputRate
        var logRequests = false
        var lockLanguage = false
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
        s.lineLift = bool(Key.lineLift, false)
        s.inflection = bool(Key.inflection, true)
        s.soTone = int(Key.soTone, 0, 25, 8)
        s.soJoin = bool(Key.soJoin, true)
        s.soShortPauses = bool(Key.soShortPauses, true)
        s.pauseMode = int(Key.pauseMode, 0, 2, 1)
        s.whine = int(Key.whine, 0, 2, 0)
        s.sampleRate = sampleRates.contains(d.integer(forKey: Key.sampleRate)) ? d.integer(forKey: Key.sampleRate)
            : outputRate
        s.logRequests = bool(Key.logRequests, false)
        s.lockLanguage = bool(Key.lockLanguage, false)
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
        n.line_lift = s.lineLift ? 1 : 0
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
    func configure(sampleRate: Int, inflection: Bool, whine: Int) {
        ssa_configure(engine, Int32(sampleRate), inflection ? 1 : 0, Int32(whine))
    }

    var sampleRate: Int { Int(ssa_sample_rate(engine)) }

    func has(_ v: SsiShared.Voice) -> Bool { ssa_has_voice(engine, v.index) != 0 }
}
