// WavExport.swift -- what Setup's "Export to WAV…" writes, Android's WavExport.kt (no Apple frameworks in it, so the
// host-side test runs it): the name the save dialog suggests -- the voice's unit, its language when not English, and
// the time, speakout-20261018-164533.wav (Tomi) -- and the 16-bit mono WAV itself, the preview's render as it is.
// The preview plays the same bytes.  MIT.

import Foundation

enum WavExport {
    /// The host-side tests' control (SSI263_IMPORT_BREAK=export; never set in the app): the header's lengths in the
    /// wrong byte order, so the read-back test must fail.
    static var control = ProcessInfo.processInfo.environment["SSI263_IMPORT_BREAK"] ?? ""

    /// Android's rule over its locale-first voice name: "en-US-speakout" at 16:45:33 -> "speakout-20261018-164533.wav";
    /// another language keeps its tag after the unit, once when the country only repeats it ("de-DE-braillelite" ->
    /// "braillelite-de-...", "pt-BR-braillelite" -> "braillelite-pt-br-..."); a name with no unit after its tag is
    /// kept whole.
    static func fileName(voiceName: String, at: Date) -> String {
        let parts = voiceName.split(separator: "-", omittingEmptySubsequences: false).map(String.init)
        func matches(_ s: String, _ lo: Int, _ hi: Int, upper: Bool) -> Bool {
            (lo...hi).contains(s.count) && s.allSatisfy { $0.isASCII && (upper ? $0.isUppercase : $0.isLowercase) }
        }
        var i = 0
        if i < parts.count && matches(parts[i], 2, 3, upper: false) { i += 1 }
        if i >= 1 && i < parts.count && matches(parts[i], 2, 2, upper: true) { i += 1 }
        if i >= parts.count { i = 0 }
        let unit = parts[i...].joined(separator: "-")
        var language: [String] = []
        for p in parts[..<i].map({ $0.lowercased() }) where !language.contains(p) { language.append(p) }
        let tag = (language.first ?? "en") == "en" ? "" : "-" + language.joined(separator: "-")
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "yyyyMMdd-HHmmss"
        return "\(unit)\(tag)-\(f.string(from: at)).wav"
    }

    /// A voice's locale-first name, as Android names its voices: its language (the system's region dropped when it
    /// is a number, ar-001 -> ar), then its unit (a Blazie key's first word -- braillelite-de is "braillelite" --
    /// the key itself otherwise: "mockingboard-early").
    static func voiceName(key: String, language: String) -> String {
        var lang = language.split(separator: "-").map(String.init)
        if lang.count > 1 && lang[1].allSatisfy(\.isNumber) { lang.removeLast() }
        let unit = key.hasPrefix("braille") ? String(key.prefix { $0 != "-" }) : key
        return lang.joined(separator: "-") + "-" + unit
    }

    /// 16-bit little-endian mono PCM as a WAV at `rate` Hz.
    static func wav(_ pcm: [Int16], rate: Int) -> Data {
        var d = Data()
        func u32(_ v: Int) {
            var x = control == "export" ? UInt32(v).bigEndian : UInt32(v).littleEndian
            d.append(Data(bytes: &x, count: 4))
        }
        func u16(_ v: Int) { var x = UInt16(v).littleEndian; d.append(Data(bytes: &x, count: 2)) }
        d.append("RIFF".data(using: .ascii)!); u32(36 + pcm.count * 2); d.append("WAVEfmt ".data(using: .ascii)!)
        u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16)
        d.append("data".data(using: .ascii)!); u32(pcm.count * 2)
        pcm.withUnsafeBufferPointer { d.append($0) }         // every Apple CPU is little-endian
        return d
    }

    /// iOS's file exporter as an export sees it (#58).  SwiftUI calls its onCompletion when the file is written or
    /// could not be, but a Cancel only closes it: "If the user cancels the operation, isPresented will be set to false
    /// and onCompletion will not be called" (fileExporter's documentation; its onCancellation came with iOS 17, and
    /// the app runs on 16).  So the exporter closing with no answer is a cancel.  Plain Swift, so the Mac's tests
    /// drive it; the control (SSI263_IMPORT_BREAK=exportcancel) lets a close with no answer pass unsaid, as before.
    struct Exporter {
        private(set) var open = false
        private var answered = false

        /// The rendered WAV handed to the exporter.
        mutating func present() {
            open = true
            answered = false
        }

        /// onCompletion's answer, in the words the app says.
        mutating func answer(_ r: Result<URL, Error>, wrote: String) -> String {
            answered = true
            switch r {
            case .success: return wrote
            case .failure(let e as CocoaError) where e.code == .userCancelled: return "Not exported."
            case .failure(let e): return "The file could not be written: \(e.localizedDescription)"
            }
        }

        /// The exporter closed: "Not exported." when no answer came (a Cancel), nil when its answer was said or it
        /// was not open.
        mutating func closed() -> String? {
            defer { open = false }
            guard open, !answered, WavExport.control != "exportcancel" else { return nil }
            return "Not exported."
        }
    }
}
