// AppModel.swift -- the app's state: which voices are imported, an import in flight (judged, confirmed, brought in
// with its progress), the removal, and the preview.  The screens (SetupView, VoiceSettingsView) show it.  MIT.

import SwiftUI
import os
#if os(iOS)
import UIKit
#else
import AppKit
#endif

private let log = Logger(subsystem: "com.ssi263speech.app", category: "app")

/// Said to VoiceOver now, whatever has focus: an import's steps and its end.
func announce(_ text: String) {
    #if os(iOS)
    UIAccessibility.post(notification: .announcement, argument: text)
    #else
    NSAccessibility.post(element: NSApp as Any, notification: .announcementRequested,
                         userInfo: [.announcement: text, .priority: NSAccessibilityPriorityLevel.high.rawValue])
    #endif
}

@MainActor
final class AppModel: ObservableObject {
    struct VoiceStatus: Identifiable {
        let voice: SsiShared.Voice
        let label: String?                  // what was imported, or nil when it is not
        var id: String { voice.key }
    }

    @Published private(set) var voices: [VoiceStatus] = []
    @Published private(set) var importing = false
    @Published private(set) var progress = 0.0
    @Published private(set) var step = ""
    @Published private(set) var pending: FirmwareImport.Plan?   // judged, waiting for the person's yes
    @Published var askingToImport = false                 // the confirmation shown; closing it is not a no
    @Published var message: String?                       // an import's or a removal's outcome, in words
    @Published var previewStatus = ""

    private var job: FirmwareInstaller.Job?
    private let preview = PreviewPlayer()
    private var lastAnnounced = Date.distantPast

    init() { refresh() }

    func refresh() {
        voices = SsiShared.voices.map { VoiceStatus(voice: $0, label: SsiShared.installed($0) ? (SsiShared.label($0) ?? "imported") : nil) }
    }

    var anyInstalled: Bool { voices.contains { $0.label != nil } }

    // ---- the import ------------------------------------------------------------------------------------------------

    /// The files the person chose: judged off the main thread, then put to them (`pending`).
    func choose(_ urls: [URL]) {
        guard !urls.isEmpty else { return }
        importing = true
        step = urls.count == 1 ? "Looking at \(urls[0].lastPathComponent)" : "Looking at \(urls.count) files"
        announce(step)
        Task.detached {
            let result = Result { try FirmwareInstaller.inspect(urls) }
            await MainActor.run {
                self.importing = false
                switch result {
                case .failure(let e):
                    self.finish(e.localizedDescription)
                case .success(let plan):
                    if let refusal = plan.refusal {
                        self.finish(refusal)
                    } else {
                        self.pending = plan
                        self.askingToImport = true
                    }
                }
            }
        }
    }

    /// The person said yes: brought in, with its progress.
    func confirm() {
        guard let plan = pending else { return }
        pending = nil
        let job = FirmwareInstaller.Job(plan: plan)
        self.job = job
        importing = true
        progress = 0
        step = "Starting"
        job.onChange = { [weak self] j in
            let (p, s) = (j.progress, j.step)
            Task { @MainActor in self?.update(progress: p, step: s) }
        }
        Task.detached {
            let result = Result { try job.commit() }
            await MainActor.run {
                self.job = nil
                self.importing = false
                switch result {
                case .success(let labels):
                    Registration.firmwareChanged()
                    self.refresh()
                    var words = "Imported: " + labels.joined(separator: "; ") + "."
                    if !plan.notes.isEmpty { words += " " + plan.notes.joined(separator: " ") }
                    if self.voices.contains(where: { $0.voice.key == "accentsa" && $0.label == nil })
                        && plan.found.contains(where: { FirmwareImport.accentRoms.contains($0.kind) }) {
                        words += " The Accent SA needs all three of u2.BIN, u3.BIN and u4.BIN; import the others " +
                            "to hear it."
                    }
                    self.finish(words)
                case .failure(let e):
                    self.finish(e is CancellationError ? "The import was cancelled; nothing was changed."
                                : "Nothing was imported. " + e.localizedDescription)
                }
            }
        }
    }

    /// Cancel: the import in flight stopped, or the one waiting for a yes dropped.  Only the person's Cancel calls
    /// it -- never the confirmation's closing, which SwiftUI does before an Import button's action runs (a closing
    /// that cancelled once dropped every confirmed import before it started).
    func cancelImport() {
        job?.cancel()
        if pending != nil {
            pending = nil
            if let s = SsiShared.stagingFolder { try? FileManager.default.removeItem(at: s) }
        }
    }

    private func update(progress p: Double, step s: String) {
        progress = p
        if s != step {
            step = s
            announce(s)
            lastAnnounced = Date()
        } else if Date().timeIntervalSince(lastAnnounced) >= 5 {
            // VoiceOver hears the bar now and then, as Android's TalkBack does: no closer than 5 s apart
            announce("\(Int(p * 100)) percent")
            lastAnnounced = Date()
        }
    }

    private func finish(_ words: String) {
        message = words
        announce(words)
        log.notice("\(words, privacy: .public)")       // an import's or a removal's outcome, for finding problems
    }

    func removeAll() {
        FirmwareInstaller.removeAll()
        Registration.firmwareChanged()
        refresh()
        finish("Every imported firmware was removed.")
    }

    // ---- the preview ----------------------------------------------------------------------------------------------

    func speak(_ text: String, voiceKey: String) {
        guard let v = SsiShared.voices.first(where: { $0.key == voiceKey }) else { return }
        previewStatus = "Speaking"
        preview.speak(text.isEmpty ? "Hello there." : text, voice: v) { [weak self] error in
            self?.previewStatus = error ?? ""
            if let e = error {
                announce(e)
                log.notice("preview: \(e, privacy: .public)")
            }
        }
    }

    func stopPreview() { preview.stop() }
}
