// The speech extension's Swift (SsiAudioUnit) as the system drives it, on the Mac: the Audio Unit made directly,
// requests handed to it and its render block pulled until it says Complete, over SSI263Core's macos-arm64 slice and
// the English Braille Lite imported from the repository's firmware (FirmwareInstaller, as the app imports it) into a
// folder of its own in place of the App Group's, with settings of its own in place of the App Group's.  No sound is
// played.  Skips, and says so, when BL2ENG.BNS is not in firmware/blazie.
//
// - A request that makes no sound, at Pauses Off, still hands over a moment of silence, so the system moves on (#61).
// - The idle sound (Hiss) survives a firmware change: a request after the stamp is bumped is the same PCM as on an
//   Audio Unit made with Hiss set, and not the same as without it (#57).
// - A stop from the system's thread while a firmware change frees the player waits until it is freed (#56); under
//   the thread sanitizer, stops hammered from another thread through firmware changes, with no hook, for the
//   sanitizer to see any unsynchronised access.
//
//     sh src/platforms/apple/test/test_extension.sh
//     SSI263_EXTENSION_BREAK=silent|whine|stop sh ...      the controls: each must FAIL
//     SSI263_SANITIZE=thread sh ...                        with the thread sanitizer (=stop: it must report a race)
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

/// A flag one thread sets and another reads, under a lock (the sanitizer's run must race on nothing of its own).
final class ManagedAtomicFlag: @unchecked Sendable {
    private let lock = NSLock()
    private var value = false
    var isSet: Bool { lock.lock(); defer { lock.unlock() }; return value }
    func set() { lock.lock(); value = true; lock.unlock() }
}

func bumpStamp() {             // what the app does on every import and removal (Registration.firmwareChanged)
    settings.set(settings.integer(forKey: SsiShared.Key.firmwareStamp) + 1, forKey: SsiShared.Key.firmwareStamp)
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

// #56: a stop from the system's thread while a firmware change frees the player.  The Audio Unit's hook runs on the
// synthesis queue at that moment, and a cancel is started then: it must wait until the old player is gone, or its
// stop could write into a freed one.  (The hook orders the two threads, so the sanitizer sees no race in this one;
// the stress run below is the sanitizer's.)
do {
    let au = audioUnit()
    _ = say(au, "Hello.")
    var waited = false, ran = false
    let hooked = DispatchSemaphore(value: 0)
    au.freeing = {
        ran = true
        let stopped = DispatchSemaphore(value: 0)
        Thread { au.cancelSpeechRequest(); stopped.signal() }.start()
        waited = stopped.wait(timeout: .now() + 0.3) == .timedOut
        hooked.signal()
    }
    bumpStamp()
    _ = say(au, "Hello.")                   // ends early: the cancel ends the request before it waits for the lock
    _ = hooked.wait(timeout: .now() + 10)
    au.freeing = nil
    check("a stop while a firmware change frees the player", ran && waited,
          !ran ? "the firmware change was not seen" : waited ? "it waited for the free"
          : "it went ahead, into the player being freed")
}

// #56 under the thread sanitizer: stops from another thread, as the system sends them, while each request after a
// stamp bump makes the engine and player again on the synthesis queue.  Without the lock (=stop) the sanitizer
// reports the race and the run exits non-zero; with it, nothing.  Only with SSI263_SANITIZE=thread: without the
// sanitizer a race this short is not seen.
if ProcessInfo.processInfo.environment["SSI263_SANITIZE"] == "thread" {
    let au = audioUnit()
    _ = say(au, "Hello.")
    let done = DispatchSemaphore(value: 0)
    let stopping = ManagedAtomicFlag()
    Thread {
        while !stopping.isSet { au.cancelSpeechRequest(); usleep(200) }
        done.signal()
    }.start()
    for _ in 0..<12 {
        bumpStamp()
        _ = say(au, "Hello.")
    }
    stopping.set()
    done.wait()
    check("stops from another thread through 12 firmware changes", true, "the sanitizer reports any race itself")
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

// #57: Hiss on, a request, then a firmware change while the same Audio Unit lives: the next request is the one an
// Audio Unit made with Hiss gives, and not the one without it (so the PCM shows the idle sound at all).
do {
    let text = "Hello."
    settings.set(0, forKey: SsiShared.Key.whine)
    let quiet = say(audioUnit(), text)
    settings.set(1, forKey: SsiShared.Key.whine)
    let hiss = say(audioUnit(), text)
    check("the idle sound is in the PCM", hiss != quiet, "Hiss \(hiss.count) frames, off \(quiet.count)")
    let au = audioUnit()
    let before = say(au, text)
    bumpStamp()
    let after = say(au, text)
    check("Hiss before a firmware change", before == hiss, "\(before.count) frames")
    check("Hiss after a firmware change", after == hiss,
          "\(after.count) frames, the same as an Audio Unit made with it")
    settings.removeObject(forKey: SsiShared.Key.whine)
}

print(failures == 0 ? "the speech extension speaks as the system drives it" : "FAILED: \(failures)")
finish()
