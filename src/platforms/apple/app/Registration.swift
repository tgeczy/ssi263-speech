// Registration.swift -- keeping the system's list of this app's voices current, the way dt-iPhone learnt on devices:
// the app (never the extension) tells the system its voices changed after an import or a removal, and at every
// launch while any firmware is there, because a restart, an update or a reinstall can leave the system holding a
// stale or empty cached list; and it makes the speech extension start now (an instance of its Audio Unit, held until
// it is made) instead of waiting for the system to, which is what once made a fresh import need a restart.  MIT.

import AVFoundation
import AudioToolbox

enum Registration {
    private static var warming: AUAudioUnit?

    /// At launch: the files' protection put right, the system nudged to find the extension after an update (as
    /// TGSpeechBox's app does), and the voice list refreshed while any firmware is there.
    static func atLaunch() {
        SsiShared.repairProtection()
        var desc = SsiShared.componentDescription
        _ = AudioComponentFindNext(nil, &desc)
        if SsiShared.voices.contains(where: SsiShared.installed) { refresh() }
    }

    /// After an import or a removal, and at launch.
    static func refresh() {
        AVSpeechSynthesisProviderVoice.updateSpeechVoices()
        warmUp()
    }

    private static func warmUp() {
        AUAudioUnit.instantiate(with: SsiShared.componentDescription, options: []) { unit, _ in
            DispatchQueue.main.async {
                warming = unit                  // held until the extension has started, then let go
                DispatchQueue.main.asyncAfter(deadline: .now() + 2) { warming = nil }
            }
        }
    }

    /// The firmware changed: the extension's engine must be made again (its units booted from the old files).
    static func firmwareChanged() {
        let d = SsiShared.defaults()
        d?.set((d?.integer(forKey: SsiShared.Key.firmwareStamp) ?? 0) + 1, forKey: SsiShared.Key.firmwareStamp)
        refresh()
    }
}
