// Upsampler.swift -- a unit's PCM at a lower rate (11025 Hz) brought up to the Audio Unit's (22050 Hz) by one
// AVAudioConverter for a whole utterance: fed block by block (.noDataNow between them) and finished once
// (.endOfStream), so its filter runs on across the blocks.  A fresh converter per block would restart the filter at
// every join -- dt-iPhone heard that as a continuous crackle.  Int16 in, Int16 out (the render state scales).  MIT.

import AVFoundation

final class Upsampler {
    private let converter: AVAudioConverter
    private let input: AVAudioFormat
    private let output: AVAudioFormat
    private let ratio: Double

    init?(from: Int, to: Int) {
        guard let i = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: Double(from), channels: 1,
                                    interleaved: false),
              let o = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: Double(to), channels: 1,
                                    interleaved: false),
              let c = AVAudioConverter(from: i, to: o) else { return nil }
        c.sampleRateConverterQuality = AVAudioQuality.max.rawValue
        converter = c
        input = i
        output = o
        ratio = Double(to) / Double(from)
    }

    /// One block in; what the converter has ready out.
    func feed(_ pcm: UnsafePointer<Int16>, _ n: Int) -> [Int16] {
        guard n > 0, let buf = AVAudioPCMBuffer(pcmFormat: input, frameCapacity: AVAudioFrameCount(n)) else { return [] }
        buf.frameLength = AVAudioFrameCount(n)
        buf.int16ChannelData![0].update(from: pcm, count: n)
        return run(buf)
    }

    /// The utterance's end: the converter's last samples.
    func finish() -> [Int16] { run(nil) }

    private func run(_ block: AVAudioPCMBuffer?) -> [Int16] {
        var given = false
        var out: [Int16] = []
        let cap = AVAudioFrameCount(Double(block?.frameLength ?? 0) * ratio) + 1024
        while true {
            guard let dst = AVAudioPCMBuffer(pcmFormat: output, frameCapacity: cap) else { return out }
            var error: NSError?
            let status = converter.convert(to: dst, error: &error) { _, inputStatus in
                if let b = block, !given {
                    given = true
                    inputStatus.pointee = .haveData
                    return b
                }
                inputStatus.pointee = block == nil ? .endOfStream : .noDataNow
                return nil
            }
            if dst.frameLength > 0 {
                out.append(contentsOf: UnsafeBufferPointer(start: dst.int16ChannelData![0], count: Int(dst.frameLength)))
            }
            if status != .haveData || dst.frameLength < cap { return out }
        }
    }
}
