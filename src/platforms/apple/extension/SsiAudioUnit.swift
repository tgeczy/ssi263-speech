// SsiAudioUnit.swift -- the system speech voice: an AVSpeechSynthesisProviderAudioUnit that speaks every imported
// unit for VoiceOver, Spoken Content and every AVSpeechSynthesizer client, as the Android app's SsiTtsService does.
//
// A request's SSML is split at VoiceOver's phrasing with the app's Pause mode (ssp_ssml.c, TGSpeechBox's rules) and
// spoken segment by segment (ssp_speech.c): each segment is the unit's own PCM, the desktop's byte for byte, at
// 22050 Hz, the Audio Unit's own rate.  The audio streams: a synthesis thread pulls the unit's 30 ms blocks into a
// buffer the render block plays from, so the first sound comes after the unit's first block, not after the whole
// request (TGSpeechBox and dt-iPhone synthesize a request whole; an emulated unit is slower than their engines).
//
// The rules learnt on devices (TGSpeechBox, dt-iPhone):
// - the voice list depends only on the files in the App Group, never on whether a unit could boot, and a file still
//   locked counts as there: an empty list is cached by the system and the voice shows as "null" until re-added;
// - the engine is made in init, synchronously, and made again on any later request that finds none;
// - the system's voice list is refreshed only by the app (after an import, and at every launch), never from here;
// - a cancel is a generation: a request hands audio over only while it is the current one and was not cancelled;
// - the render block completes only when the buffer is drained and the request finished, since an empty buffer is
//   also what a request looks like before its first block arrives; it captures the buffer, not the unit;
// - an empty request completes with a moment of silence, never a demo phrase.
//
// MIT.

import AVFoundation
import os

private let log = Logger(subsystem: "com.ssi263speech.app.synth-extension", category: "speech")

/// The audio between the synthesis thread and the render block, for one request at a time (its generation).
final class RenderState: @unchecked Sendable {
    private let lock = NSCondition()
    private var samples: [Float32] = []
    private var offset = 0
    private var generation = 0
    private var finished = true

    /// A new request: everything before it dropped.  Its generation.
    func begin() -> Int {
        lock.lock(); defer { lock.unlock() }
        generation += 1
        samples.removeAll(keepingCapacity: true)
        offset = 0
        finished = false
        lock.broadcast()
        return generation
    }

    /// A cancel: the request dropped and finished, so the render block completes at once.
    func cancel() {
        lock.lock(); defer { lock.unlock() }
        generation += 1
        samples.removeAll(keepingCapacity: true)
        offset = 0
        finished = true
        lock.broadcast()
    }

    func isCurrent(_ g: Int) -> Bool {
        lock.lock(); defer { lock.unlock() }
        return g == generation
    }

    /// Int16 PCM from the unit, scaled by the request's volume.  False when the request is no longer current.
    func append(_ g: Int, _ pcm: UnsafePointer<Int16>, _ n: Int, volume: Float32) -> Bool {
        lock.lock(); defer { lock.unlock() }
        guard g == generation else { return false }
        let scale = volume / 32768
        samples.reserveCapacity(samples.count + n)
        for i in 0..<n { samples.append(Float32(pcm[i]) * scale) }
        lock.broadcast()
        return true
    }

    func finish(_ g: Int) {
        lock.lock(); defer { lock.unlock() }
        guard g == generation else { return }
        finished = true
        lock.broadcast()
    }

    /// `frames` samples into out -- waiting for the synthesis thread until it has made that many, or has finished --
    /// and whether the request is complete (drained and finished).  Never fewer while the request runs: the system
    /// plays every frame it asked for, so a short buffer's padding was heard as a gap (the Speak-Out came out 13 %
    /// long, a few zeros after every block, while the system pulled faster than the unit made its blocks).
    func render(into out: UnsafeMutablePointer<Float32>, frames: Int) -> (Int, Bool) {
        lock.lock(); defer { lock.unlock() }
        let deadline = Date(timeIntervalSinceNow: 2)   // a unit that has stopped answering must not hang the system
        while samples.count - offset < frames && !finished {
            if !lock.wait(until: deadline) { break }
        }
        let n = min(samples.count - offset, frames)
        if n > 0 {
            samples.withUnsafeBufferPointer { out.update(from: $0.baseAddress! + offset, count: n) }
            offset += n
        }
        if offset >= samples.count {
            if offset > 1 << 20 {          // a long request: the played part let go
                samples.removeAll(keepingCapacity: false)
                offset = 0
            }
            return (n, finished)
        }
        return (n, false)
    }
}

public final class SsiAudioUnit: AVSpeechSynthesisProviderAudioUnit {
    private let outputBus: AUAudioUnitBus
    private var busses: AUAudioUnitBusArray!
    private let state = RenderState()

    // The engine and the player belong to the synthesis queue; ssp_speech_stop alone is called from anywhere.
    private let synthesis = DispatchQueue(label: "com.ssi263speech.synthesis", qos: .userInteractive)
    private var engine: SsiEngine?
    private var player: OpaquePointer?
    private var inflection = true
    private var sampleRate = SsiShared.outputRate
    private var firmwareStamp = -1

    public override init(componentDescription: AudioComponentDescription,
                         options: AudioComponentInstantiationOptions = []) throws {
        var asbd = AudioStreamBasicDescription(
            mSampleRate: Double(SsiShared.outputRate), mFormatID: kAudioFormatLinearPCM,
            mFormatFlags: kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved,
            mBytesPerPacket: 4, mFramesPerPacket: 1, mBytesPerFrame: 4, mChannelsPerFrame: 1, mBitsPerChannel: 32,
            mReserved: 0)
        guard let format = AVAudioFormat(streamDescription: &asbd) else {
            throw NSError(domain: NSOSStatusErrorDomain, code: Int(kAudioUnitErr_FormatNotSupported))
        }
        outputBus = try AUAudioUnitBus(format: format)
        try super.init(componentDescription: componentDescription, options: options)
        busses = AUAudioUnitBusArray(audioUnit: self, busType: .output, busses: [outputBus])
        synthesis.sync { ensureEngine() }
    }

    deinit {
        if let p = player { ssp_speech_free(p) }
    }

    /// On the synthesis queue: the engine, made now if it is not yet (the App Group may only now be readable), and
    /// made again when the app has changed the firmware since (its units were booted from the old files).
    private func ensureEngine() {
        let stamp = SsiShared.defaults()?.integer(forKey: SsiShared.Key.firmwareStamp) ?? 0
        if stamp != firmwareStamp, engine != nil {
            if let p = player { ssp_speech_free(p) }
            player = nil
            engine = nil
        }
        firmwareStamp = stamp
        if engine == nil, let e = SsiEngine() {
            engine = e
            player = ssp_speech_new(e.engine)
            if player == nil { engine = nil }
            inflection = true                   // SsiEngine boots with inflection on, at the Audio Unit's rate
            sampleRate = SsiShared.outputRate
        }
    }

    // MARK: - Voices

    public override var speechVoices: [AVSpeechSynthesisProviderVoice] {
        get {
            SsiShared.repairProtection()
            let container = SsiShared.unitFolder != nil
            return SsiShared.voices.filter { !container || SsiShared.installed($0) }.map { v in
                let voice = AVSpeechSynthesisProviderVoice(name: v.name, identifier: SsiShared.identifierPrefix + v.key,
                                                           primaryLanguages: [v.language],
                                                           supportedLanguages: [v.language])
                voice.version = "1.0"
                return voice
            }
        }
        set {}
    }

    // MARK: - Audio Unit

    public override var outputBusses: AUAudioUnitBusArray { busses }

    public override var internalRenderBlock: AUInternalRenderBlock {
        let state = self.state
        return { actionFlags, _, frameCount, _, outputData, _, _ in
            let list = UnsafeMutableAudioBufferListPointer(outputData)
            guard let out = list[0].mData?.assumingMemoryBound(to: Float32.self) else { return noErr }
            let frames = Int(frameCount)
            out.initialize(repeating: 0, count: frames)
            let (n, complete) = state.render(into: out, frames: frames)
            list[0].mDataByteSize = UInt32(n * MemoryLayout<Float32>.size)
            if complete { actionFlags.pointee = .offlineUnitRenderAction_Complete }
            return noErr
        }
    }

    // MARK: - Synthesis

    public override func synthesizeSpeechRequest(_ request: AVSpeechSynthesisProviderRequest) {
        let generation = state.begin()
        if let p = player { ssp_speech_stop(p) }          // a request still being made: let go before its next block
        let settings = SsiShared.load()
        let ssml = request.ssmlRepresentation
        let identifier = request.voice.identifier
        if settings.logRequests {
            log.notice("request \(generation, privacy: .public) \(identifier, privacy: .public): \(ssml, privacy: .public)")
        } else {                                       // debug: seen only by a live `log stream --level debug`
            log.debug("request \(generation, privacy: .public) \(identifier, privacy: .public): \(ssml, privacy: .public)")
        }
        synthesis.async { [self] in
            speak(generation, ssml: ssml, identifier: identifier, settings: settings)
        }
    }

    /// On the synthesis queue: the request, block by block, into the render state while it is the current one.
    private func speak(_ generation: Int, ssml: String, identifier: String, settings: SsiShared.Settings) {
        defer { state.finish(generation) }
        guard state.isCurrent(generation) else { return }
        ensureEngine()
        guard let e = engine, let p = player else {
            log.error("no engine: the App Group is not readable")
            return
        }
        var request = ssp_request()
        guard ssp_parse(ssml, Int32(settings.pauseMode), &request) == 0 else { return }
        defer { ssp_request_free(&request) }
        let volume = Float32(request.volume)
        // An empty request, or one that is only VoiceOver's pause: its silence (a moment when it has none), so the
        // system moves on.
        guard ssp_has_text(&request) != 0 else {
            let n = max(1, Int(ssp_total_pause_ms(&request)) * SsiShared.outputRate / 1000)
            let zeros = [Int16](repeating: 0, count: min(n, SsiShared.outputRate * 2))
            zeros.withUnsafeBufferPointer { _ = state.append(generation, $0.baseAddress!, $0.count, volume: 1) }
            return
        }
        guard let voice = SsiShared.voice(identifier: identifier) ?? SsiShared.voices.first(where: { e.has($0) })
        else { return }
        if settings.inflection != inflection || settings.sampleRate != sampleRate {
            e.configure(sampleRate: settings.sampleRate, inflection: settings.inflection)
            inflection = settings.inflection
            sampleRate = settings.sampleRate
        }
        e.loadAicom()
        var native = SsiShared.native(settings)
        guard ssp_speech_start(p, voice.index, &request, &native) == 0 else { return }
        // the units' rate brought up to the Audio Unit's, one converter for the whole utterance
        let upsampler = e.sampleRate == SsiShared.outputRate ? nil : Upsampler(from: e.sampleRate, to: SsiShared.outputRate)
        var buf = [Int16](repeating: 0, count: 2048)
        func hand(_ pcm: UnsafePointer<Int16>, _ n: Int) -> Bool {
            guard let up = upsampler else { return state.append(generation, pcm, n, volume: volume) }
            return up.feed(pcm, n).withUnsafeBufferPointer {
                $0.isEmpty || state.append(generation, $0.baseAddress!, $0.count, volume: volume)
            }
        }
        while true {
            let n = buf.withUnsafeMutableBufferPointer { ssp_speech_pull(p, $0.baseAddress, Int32($0.count)) }
            if n <= 0 {
                if n == 0, let up = upsampler {
                    _ = up.finish().withUnsafeBufferPointer {
                        $0.isEmpty || state.append(generation, $0.baseAddress!, $0.count, volume: volume)
                    }
                }
                break
            }
            let current = buf.withUnsafeBufferPointer { hand($0.baseAddress!, Int(n)) }
            if !current {
                ssp_speech_stop(p)
                _ = buf.withUnsafeMutableBufferPointer { ssp_speech_pull(p, $0.baseAddress, Int32($0.count)) }
                break
            }
        }
        if ssp_speech_errors(p) > 0 {
            log.error("\(voice.key, privacy: .public): \(ssp_speech_errors(p)) segment(s) the unit refused")
        }
    }

    public override func cancelSpeechRequest() {
        state.cancel()
        if let p = player { ssp_speech_stop(p) }
    }
}
