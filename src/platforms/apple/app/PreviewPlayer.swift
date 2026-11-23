// PreviewPlayer.swift -- the app's own preview: a text spoken through the same C as the speech extension (ssp_ssml,
// ssp_speech over ssa_engine), at the app's settings and the unit's own sample rate, and played from memory as a WAV
// with AVAudioPlayer (AVAudioEngine would ask for the microphone -- TGSpeechBox).  Rendered off the main thread.
// Export to WAV is the same render, handed back to be written where the person chooses (WavExport).  MIT.

import AVFoundation
import os

final class PreviewPlayer: NSObject, AVAudioPlayerDelegate {
    private let queue = DispatchQueue(label: "com.ssi263speech.preview", qos: .userInitiated)
    private var engine: SsiEngine?
    private var player: AVAudioPlayer?
    private var engineStamp = -1
    private let cancel = OSAllocatedUnfairLock(initialState: false)   // Stop during a render: nothing comes of it

    /// Speaks `text` on `voice`; `done` on the main thread with nil, or the reason it could not.
    func speak(_ text: String, voice: SsiShared.Voice, done: @escaping (String?) -> Void) {
        player?.stop()
        cancel.withLock { $0 = false }
        let settings = SsiShared.load()
        queue.async { [self] in
            let result = render(text, voice: voice, settings: settings)
            let stopped = cancel.withLock { $0 }
            DispatchQueue.main.async {
                switch result {
                case _ where stopped: done(nil)             // Stop during the render: nothing is played
                case .failure(let e): done(e.message)
                case .success(let wav):
                    do {
                        #if os(iOS)
                        try? AVAudioSession.sharedInstance().setCategory(.playback, mode: .spokenAudio)
                        try? AVAudioSession.sharedInstance().setActive(true)
                        #endif
                        let p = try AVAudioPlayer(data: wav)
                        p.delegate = self
                        p.volume = 1
                        self.player = p
                        p.play()
                        done(nil)
                    } catch {
                        done("The preview could not be played: \(error.localizedDescription)")
                    }
                }
            }
        }
    }

    func stop() {
        cancel.withLock { $0 = true }
        player?.stop()
    }

    /// What an export's render came to.
    enum Export {
        case rendered(Data, seconds: Double)        // the WAV, to be written where the person chooses
        case stopped                                // Stop during the render: nothing to write
        case failed(String)
    }

    /// `text` rendered on `voice` as Speak would, never played; `done` on the main thread.
    func export(_ text: String, voice: SsiShared.Voice, done: @escaping (Export) -> Void) {
        player?.stop()
        cancel.withLock { $0 = false }
        let settings = SsiShared.load()
        queue.async { [self] in
            let result = render(text, voice: voice, settings: settings)
            let stopped = cancel.withLock { $0 }
            DispatchQueue.main.async {
                switch result {
                case _ where stopped: done(.stopped)
                case .failure(let e): done(.failed(e.message))
                case .success(let wav):
                    let rate = Double(wav.subdata(in: 24..<28).withUnsafeBytes { $0.loadUnaligned(as: UInt32.self) })
                    done(.rendered(wav, seconds: Double(wav.count - 44) / 2 / max(rate, 1)))
                }
            }
        }
    }

    private struct Failure: Error { let message: String }

    private func render(_ text: String, voice: SsiShared.Voice, settings: SsiShared.Settings) -> Result<Data, Failure> {
        let stamp = SsiShared.defaults()?.integer(forKey: SsiShared.Key.firmwareStamp) ?? 0
        if engine == nil || stamp != engineStamp {
            engine = SsiEngine()
            engineStamp = stamp
        }
        guard let e = engine else { return .failure(Failure(message: "The app's shared folder is not available.")) }
        e.configure(sampleRate: settings.sampleRate, inflection: settings.inflection, whine: settings.whine)
        e.loadAicom()
        guard e.has(voice) else { return .failure(Failure(message: "The \(voice.name)'s firmware is not imported.")) }
        guard let p = ssp_speech_new(e.engine) else { return .failure(Failure(message: "Out of memory.")) }
        defer { ssp_speech_free(p) }
        var request = ssp_request()
        guard ssp_parse_text(text, Int32(settings.pauseMode), &request) == 0 else {   // typed, never SSML (#59)
            return .failure(Failure(message: "Out of memory."))
        }
        defer { ssp_request_free(&request) }
        var native = SsiShared.native(settings)
        var pcm: [Int16] = []
        if ssp_speech_start(p, voice.index, &request, &native) == 0 {
            var buf = [Int16](repeating: 0, count: 4096)
            while !cancel.withLock({ $0 }) {
                let n = buf.withUnsafeMutableBufferPointer { ssp_speech_pull(p, $0.baseAddress, Int32($0.count)) }
                if n <= 0 { break }
                pcm.append(contentsOf: buf[0..<Int(n)])
            }
        }
        if ssp_speech_errors(p) > 0 { return .failure(Failure(message: "The \(voice.name) unit would not speak.")) }
        if pcm.isEmpty { return .failure(Failure(message: "There was nothing to say.")) }
        return .success(WavExport.wav(pcm, rate: e.sampleRate))
    }
}
