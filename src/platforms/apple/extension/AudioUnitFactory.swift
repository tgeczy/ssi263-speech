// AudioUnitFactory.swift -- the speech extension's principal class: the system makes it through NSExtensionPrincipalClass
// and asks it for the Audio Unit (SsiAudioUnit).  MIT.

import AVFoundation

public class AudioUnitFactory: NSObject, AUAudioUnitFactory {
    public func beginRequest(with context: NSExtensionContext) {}

    @objc public func createAudioUnit(with componentDescription: AudioComponentDescription) throws -> AUAudioUnit {
        try SsiAudioUnit(componentDescription: componentDescription, options: [])
    }
}
