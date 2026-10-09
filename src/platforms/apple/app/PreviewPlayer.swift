// PreviewPlayer.swift -- the app's own preview: a text spoken through the same C as the speech extension (ssp_ssml,
// ssp_speech over ssa_engine), at the app's settings and the unit's own sample rate, and played from memory as a WAV
// with AVAudioPlayer (AVAudioEngine would ask for the microphone -- TGSpeechBox).  Rendered off the main thread.  MIT.

import AVFoundation

final class PreviewPlayer: NSObject, AVAudioPlayerDelegate {
    private let queue = DispatchQueue(label: "com.ssi263speech.preview", qos: .userInitiated)
    private var engine: SsiEngine?
    private var player: AVAudioPlayer?
    private var engineStamp = -1

    /// Speaks `text` on `voice`; `done` on the main thread with nil, or the reason it could not.
    func speak(_ text: String, voice: SsiShared.Voice, done: @escaping (String?) -> Void) {
        player?.stop()
        let settings = SsiShared.load()
        queue.async { [self] in
            let result = render(text, voice: voice, settings: settings)
            DispatchQueue.main.async {
                switch result {
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

    func stop() { player?.stop() }

    private struct Failure: Error { let message: String }

    private func render(_ text: String, voice: SsiShared.Voice, settings: SsiShared.Settings) -> Result<Data, Failure> {
        let stamp = SsiShared.defaults()?.integer(forKey: SsiShared.Key.firmwareStamp) ?? 0
        if engine == nil || stamp != engineStamp {
            engine = SsiEngine()
            engineStamp = stamp
        }
        guard let e = engine else { return .failure(Failure(message: "The app's shared folder is not available.")) }
        e.configure(sampleRate: settings.sampleRate, inflection: settings.inflection)
        e.loadAicom()
        guard e.has(voice) else { return .failure(Failure(message: "The \(voice.name)'s firmware is not imported.")) }
        guard let p = ssp_speech_new(e.engine) else { return .failure(Failure(message: "Out of memory.")) }
        defer { ssp_speech_free(p) }
        var request = ssp_request()
        guard ssp_parse(text, Int32(settings.pauseMode), &request) == 0 else {
            return .failure(Failure(message: "Out of memory."))
        }
        defer { ssp_request_free(&request) }
        var native = SsiShared.native(settings)
        var pcm: [Int16] = []
        if ssp_speech_start(p, voice.index, &request, &native) == 0 {
            var buf = [Int16](repeating: 0, count: 4096)
            while true {
                let n = buf.withUnsafeMutableBufferPointer { ssp_speech_pull(p, $0.baseAddress, Int32($0.count)) }
                if n <= 0 { break }
                pcm.append(contentsOf: buf[0..<Int(n)])
            }
        }
        if ssp_speech_errors(p) > 0 { return .failure(Failure(message: "The \(voice.name) unit would not speak.")) }
        if pcm.isEmpty { return .failure(Failure(message: "There was nothing to say.")) }
        return .success(wav(pcm, rate: e.sampleRate))
    }

    private func wav(_ pcm: [Int16], rate: Int) -> Data {
        var d = Data()
        func u32(_ v: Int) { var x = UInt32(v).littleEndian; d.append(Data(bytes: &x, count: 4)) }
        func u16(_ v: Int) { var x = UInt16(v).littleEndian; d.append(Data(bytes: &x, count: 2)) }
        d.append("RIFF".data(using: .ascii)!); u32(36 + pcm.count * 2); d.append("WAVEfmt ".data(using: .ascii)!)
        u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16)
        d.append("data".data(using: .ascii)!); u32(pcm.count * 2)
        pcm.withUnsafeBufferPointer { d.append(UnsafeBufferPointer(start: $0.baseAddress, count: $0.count)) }
        return d
    }
}
