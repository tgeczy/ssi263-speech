// The speech extension's Swift (SsiAudioUnit) as the system drives it, on the Mac: the Audio Unit made directly,
// requests handed to it and its render block pulled until it says Complete, over SSI263Core's macos-arm64 slice and
// the English Braille Lite imported from the repository's firmware (FirmwareInstaller, as the app imports it) into a
// folder of its own in place of the App Group's, with settings of its own in place of the App Group's.  No sound is
// played.  Skips, and says so, when BL2ENG.BNS is not in firmware/blazie.
//
// - A request that makes no sound, at Pauses Off, still hands over a moment of silence, so the system moves on (#61).
//
//     sh src/platforms/apple/test/test_extension.sh
//     SSI263_EXTENSION_BREAK=silent sh ...      the controls: each must FAIL
import AVFoundation

let repo = URL(fileURLWithPath: CommandLine.arguments[1])
let root = FileManager.default.temporaryDirectory.appendingPathComponent("ssi263-extension-\(getpid())")
try? FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
SsiShared.containerOverride = root
let suite = "ssi263-extension-test-\(getpid())"
let settings = UserDefaults(suiteName: suite)!
SsiShared.defaultsOverride = settings
var failures = 0

func finish() -> Never {
    settings.removePersistentDomain(forName: suite)
    try? FileManager.default.removeItem(at: root)
    exit(failures == 0 ? 0 : 1)
}

func check(_ label: String, _ ok: Bool, _ detail: String) {
    print("\(ok ? "ok   " : "FAIL ") \(label): \(detail)")
    if !ok { failures += 1 }
}

let bns = repo.appendingPathComponent("firmware/blazie/BL2ENG.BNS")
guard FileManager.default.fileExists(atPath: bns.path) else {
    print("skip  the speech extension: no firmware/blazie/BL2ENG.BNS")
    finish()
}
do {
    _ = try FirmwareInstaller.Job(plan: try FirmwareInstaller.inspect([bns])).commit()
} catch {
    check("the English Braille Lite imported", false, error.localizedDescription)
    finish()
}
let lite = SsiShared.voices.first { $0.key == "braillelite" }!
let voice = AVSpeechSynthesisProviderVoice(name: "Braille Lite 2000 (en-US)",
                                           identifier: SsiShared.identifier(lite, "en-US"),
                                           primaryLanguages: ["en-US"], supportedLanguages: ["en-US"])

func audioUnit() -> SsiAudioUnit {
    try! SsiAudioUnit(componentDescription: SsiShared.componentDescription, options: [])
}

/// A request handed over as the system hands it, and the render block pulled 512 frames at a time until it says
/// Complete: every frame it handed over.
func say(_ au: SsiAudioUnit, _ ssml: String) -> [Float32] {
    au.synthesizeSpeechRequest(AVSpeechSynthesisProviderRequest(ssmlRepresentation: ssml, voice: voice))
    let render = au.internalRenderBlock
    let frames = 512
    let data = UnsafeMutablePointer<Float32>.allocate(capacity: frames)
    defer { data.deallocate() }
    var list = AudioBufferList(mNumberBuffers: 1,
                               mBuffers: AudioBuffer(mNumberChannels: 1, mDataByteSize: 0, mData: nil))
    var stamp = AudioTimeStamp()
    var out: [Float32] = []
    for _ in 0..<20_000 {                                   // about seven minutes of audio at most
        var flags = AudioUnitRenderActionFlags()
        list.mBuffers.mData = UnsafeMutableRawPointer(data)
        list.mBuffers.mDataByteSize = UInt32(frames * MemoryLayout<Float32>.size)
        _ = render(&flags, &stamp, AUAudioFrameCount(frames), 0, &list, nil, nil)
        out.append(contentsOf: UnsafeBufferPointer(start: data, count: Int(list.mBuffers.mDataByteSize) / 4))
        if flags.contains(.offlineUnitRenderAction_Complete) { break }
    }
    return out
}

// #61: at Pauses Off a request with text that makes no sound -- a non-breaking space alone, which the SSML counts as
// text and the unit as a space -- still hands over a moment, as an empty request does.
settings.set(0, forKey: SsiShared.Key.pauseMode)
do {
    let au = audioUnit()
    let hello = say(au, "Hello.")
    check("a request at Pauses Off", hello.contains { $0 != 0 }, "\(hello.count) frames")
    let silent = say(au, "\u{00A0}")
    check("a request with text that makes no sound, at Pauses Off", !silent.isEmpty,
          "\(silent.count) frame(s), at least one so the system moves on")
    let empty = say(au, "")
    check("an empty request, at Pauses Off", !empty.isEmpty, "\(empty.count) frame(s)")
}
settings.removeObject(forKey: SsiShared.Key.pauseMode)

print(failures == 0 ? "the speech extension speaks as the system drives it" : "FAILED: \(failures)")
finish()
