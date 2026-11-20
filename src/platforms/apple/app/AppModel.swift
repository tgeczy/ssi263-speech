// AppModel.swift -- the app's state: which voices are imported, an import in flight (judged, confirmed, brought in
// with its progress), the removal, and the preview.  The screens (SetupView, VoiceSettingsView) show it.  MIT.

import SwiftUI
import UniformTypeIdentifiers
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

    /// A release no voice speaks, kept for the unit tab alone (the Braille 'n Speak 640, Japanese, the betas).
    struct UnitStatus: Identifiable {
        let id: String                      // its row's id: units/<id>.BNS
        let name: String                    // "Braille 'n Speak 640 (English)"
        let bring: String                   // the file a person brings: BSPENG.BNS
        let label: String?                  // what was imported, or nil when it is not
    }

    @Published private(set) var voices: [VoiceStatus] = []
    @Published private(set) var units: [UnitStatus] = []
    @Published private(set) var importing = false
    @Published private(set) var progress = 0.0
    @Published private(set) var step = ""
    @Published private(set) var pending: FirmwareImport.Plan?   // judged, waiting for the person's yes
    @Published var askingToImport = false                 // the confirmation shown; closing it is not a no
    @Published var message: String?                       // an import's or a removal's outcome, in words
    @Published var previewStatus = ""
    @Published private(set) var exporting = false           // an export's render in flight
    @Published var exportDocument: WavDocument?             // iOS: the rendered WAV, for the file exporter
    @Published var showExporter = false
    @Published private(set) var exportName = ""             // the name the save dialog suggests (WavExport)
    private var exportSeconds = 0.0

    private var job: FirmwareInstaller.Job?
    private let preview = PreviewPlayer()
    private var lastAnnounced = Date.distantPast

    init() { refresh() }

    func refresh() {
        voices = SsiShared.voices.map { VoiceStatus(voice: $0, label: SsiShared.installed($0) ? (SsiShared.label($0) ?? "imported") : nil) }
        // the table's releases no voice speaks (Android's nativeUnitReleases), each with what is here of it
        let dir = SsiShared.unitFolder
        units = (0..<blv_firmware_count()).filter { blv_firmware_language($0) == BLV_FW_NONE
                                                    && blv_release_voice_key($0) == nil }.compactMap { k in
            guard let id = blv_release_id(k).map({ String(cString: $0) }) else { return nil }
            let name = blv_release_name(k).map { String(cString: $0) } ?? id
            let file = dir?.appendingPathComponent("units/\(id).BNS")
            let here = file.map { FileManager.default.fileExists(atPath: $0.path) } ?? false
            let label = here ? ((try? String(contentsOf: file!.appendingPathExtension("label"), encoding: .utf8))
                                ?? name) : nil
            return UnitStatus(id: id, name: name, bring: blv_release_file(k).map { String(cString: $0) } ?? "",
                              label: label)
        }
    }

    /// Anything imported: a voice's firmware, or a unit's for the unit tab alone (Remove then has something to do).
    var anyInstalled: Bool { voices.contains { $0.label != nil } || units.contains { $0.label != nil } }

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
        let awake = KeepAwake()                 // Android's FLAG_KEEP_SCREEN_ON: Spanish's minute outlasts a lock
        job.onChange = { [weak self] j in
            let (p, s) = (j.progress, j.step)
            Task { @MainActor in self?.update(progress: p, step: s) }
        }
        Task.detached {
            let result = Result { try job.commit() }
            await MainActor.run {
                awake.end()
                self.job = nil
                self.importing = false
                switch result {
                case .success:
                    Registration.firmwareChanged()
                    self.refresh()
                    // the voices that speak now, and a unit for the unit tab alone said to be no voice
                    var words = FirmwareImport.doneMessage(plan.found, NativeIdentify())
                    if !plan.notes.isEmpty { words += " " + plan.notes.joined(separator: " ") }
                    if self.voices.contains(where: { $0.voice.key == "accentsa" && $0.label == nil })
                        && plan.found.contains(where: { FirmwareImport.accentRoms.contains($0.kind) }) {
                        words += " The Accent-PC needs all three of u2.BIN, u3.BIN and u4.BIN; import the others " +
                            "to hear it."
                    }
                    self.finish(words)
                case .failure(let e):
                    self.finish(e is CancellationError ? "Import cancelled. Nothing was changed."
                                : "Import failed. Nothing was changed. " + e.localizedDescription)
                }
            }
        }
    }

    /// Cancel: the import in flight stopped, or the one waiting for a yes dropped.  Only the person's Cancel calls
    /// it -- never the confirmation's closing, which SwiftUI does before an Import button's action runs (a closing
    /// that cancelled once dropped every confirmed import before it started).
    func cancelImport() {
        if let j = job {
            j.cancel()
            step = "Cancelling"
            announce(step)
        }
        if pending != nil {
            pending = nil
            finish("Not imported.")
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

    /// Export to WAV: the text rendered as Speak would, then written where the person chooses -- the save panel on
    /// the Mac, the file exporter on iOS (Views).  Rendered first, so a Stop during it leaves nothing; the file is
    /// written whole or not at all.
    func export(_ text: String, voiceKey: String) {
        guard let v = SsiShared.voices.first(where: { $0.key == voiceKey }), !exporting else { return }
        exporting = true
        previewStatus = "Exporting…"
        announce(previewStatus)
        preview.export(text.isEmpty ? "Hello there." : text, voice: v) { [weak self] r in
            guard let self = self else { return }
            switch r {
            case .stopped: self.exported("Stopped: nothing was exported.")
            case .failed(let m): self.exported(m)
            case .rendered(let wav, let seconds):
                self.exportSeconds = seconds
                self.exportName = WavExport.fileName(voiceName: WavExport.voiceName(key: v.key, language: v.language),
                                                     at: Date())
                #if os(macOS)
                let panel = NSSavePanel()
                panel.nameFieldStringValue = self.exportName
                panel.allowedContentTypes = [.wav]
                panel.canCreateDirectories = true
                guard panel.runModal() == .OK, let url = panel.url else { return self.exported("Not exported.") }
                do {
                    try wav.write(to: url, options: .atomic)    // whole, or nothing left behind
                    self.exported(self.wrote)
                } catch {
                    self.exported("The file could not be written: \(error.localizedDescription)")
                }
                #else
                self.exportDocument = WavDocument(data: wav)
                self.showExporter = true
                #endif
            }
        }
    }

    /// The file exporter's answer (iOS).
    func exportFinished(_ r: Result<URL, Error>) {
        exportDocument = nil
        switch r {
        case .success: exported(wrote)
        case .failure(let e as CocoaError) where e.code == .userCancelled: exported("Not exported.")
        case .failure(let e): exported("The file could not be written: \(e.localizedDescription)")
        }
    }

    private var wrote: String { String(format: "Exported %.1f seconds to the file.", exportSeconds) }

    private func exported(_ s: String) {
        exporting = false
        previewStatus = s
        announce(s)
        log.notice("export: \(s, privacy: .public)")
    }
}

/// The device kept from sleeping (iOS: the screen from locking) while an import runs, and let go after.
@MainActor
final class KeepAwake {
    #if os(iOS)
    private let was = UIApplication.shared.isIdleTimerDisabled
    init() { UIApplication.shared.isIdleTimerDisabled = true }
    func end() { UIApplication.shared.isIdleTimerDisabled = was }
    #else
    private let activity = ProcessInfo.processInfo.beginActivity(
        options: [.userInitiated, .idleSystemSleepDisabled], reason: "Importing firmware")
    func end() { ProcessInfo.processInfo.endActivity(activity) }
    #endif
}
