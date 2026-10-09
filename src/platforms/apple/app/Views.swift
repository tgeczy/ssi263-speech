// Views.swift -- the app's screens, as the Android app's two pages: Setup (the voices, the firmware's import and
// removal, a preview, where to choose the voice, the licences) and Voice settings (every voice's, and each unit's
// own).  Built for VoiceOver first: every control says its value, headings mark the sections, the import's progress
// is announced.  MIT.

import SwiftUI
import UniformTypeIdentifiers

@main
struct SsiSpeechApp: App {
    @StateObject private var model = AppModel()

    init() { Registration.atLaunch() }

    var body: some Scene {
        WindowGroup {
            ContentView().environmentObject(model)
        }
    }
}

struct ContentView: View {
    var body: some View {
        TabView {
            NavigationStack { SetupView() }
                .tabItem { Label("Setup", systemImage: "square.and.arrow.down") }
            NavigationStack { VoiceSettingsView() }
                .tabItem { Label("Voice settings", systemImage: "slider.horizontal.3") }
        }
    }
}

// ---- Setup ------------------------------------------------------------------------------------------------------------

struct SetupView: View {
    @EnvironmentObject var model: AppModel
    @State private var picking = false
    @State private var confirmRemove = false
    @State private var showLicenses = false
    @AppStorage(SsiShared.Key.voice, store: SsiShared.defaults()) private var previewVoice = "accentsa"
    @State private var text = "Hello there. This is the SSI-263, speaking on Apple."

    var body: some View {
        Form {
            Section {
                ForEach(model.voices) { v in
                    VStack(alignment: .leading) {
                        Text(v.voice.name).font(.headline)
                        Text(v.label ?? "Not imported").font(.subheadline)
                    }
                    .accessibilityElement(children: .combine)
                }
            } header: {
                Text("Voices").accessibilityAddTraits(.isHeader)
            } footer: {
                Text("This app carries no firmware. Each voice speaks once you import the firmware you own: the " +
                     "Braille Lite 2000's, GW Micro's Speak-Out's, and Aicom's for the Accent SA and Accent-mini.")
            }

            Section {
                if model.importing {
                    VStack(alignment: .leading) {
                        Text(model.step)
                        ProgressView(value: model.progress)
                            .accessibilityLabel("Import progress")
                            .accessibilityValue("\(Int(model.progress * 100)) percent")
                    }
                    Button("Cancel the import", role: .cancel) { model.cancelImport() }
                } else {
                    Button("Import firmware…") { picking = true }
                    if model.anyInstalled {
                        Button("Remove all firmware…", role: .destructive) { confirmRemove = true }
                    }
                }
                if let m = model.message {
                    Text(m).textSelection(.enabled)
                }
            } header: {
                Text("Firmware").accessibilityAddTraits(.isHeader)
            } footer: {
                Text("Choose firmware files you own, update programs, or .zip or .nvda-addon packages containing " +
                     "them, one or several at once. Each file is known by its contents, whatever its name.")
            }

            Section {
                let imported = model.voices.filter { $0.label != nil }
                if imported.isEmpty {
                    Text("Import a voice's firmware first.")
                } else {
                    Picker("Voice", selection: $previewVoice) {
                        ForEach(imported) { v in Text(v.voice.name).tag(v.voice.key) }
                    }
                    TextField("Text to speak", text: $text, axis: .vertical)
                    Button("Speak") { model.speak(text, voiceKey: previewVoice) }
                    Button("Stop") { model.stopPreview() }
                }
                if !model.previewStatus.isEmpty { Text(model.previewStatus) }
            } header: {
                Text("Preview").accessibilityAddTraits(.isHeader)
            }

            Section {
                #if os(iOS)
                Text("In Settings, Accessibility, VoiceOver, Speech, add a voice and choose SSI-263 Speech. Spoken " +
                     "Content takes it the same way.")
                #else
                Text("In System Settings, Accessibility, VoiceOver, open the VoiceOver Utility's Speech category and " +
                     "choose a voice from SSI-263 Speech. Spoken Content takes it the same way.")
                #endif
            } header: {
                Text("Using the voices").accessibilityAddTraits(.isHeader)
            }

            Section {
                Button("Licenses and source") { showLicenses = true }
            }
        }
        .navigationTitle("SSI-263 Speech")
        .fileImporter(isPresented: $picking, allowedContentTypes: [.item], allowsMultipleSelection: true) { r in
            if case .success(let urls) = r { model.choose(urls) }
        }
        .confirmationDialog("Import this firmware?", isPresented: Binding(
            get: { model.pending != nil }, set: { if !$0 { model.cancelImport() } }), titleVisibility: .visible) {
            Button("Import") { model.confirm() }
            Button("Cancel", role: .cancel) { model.cancelImport() }
        } message: {
            Text(planWords(model.pending))
        }
        .confirmationDialog("Remove all firmware?", isPresented: $confirmRemove, titleVisibility: .visible) {
            Button("Remove", role: .destructive) { model.removeAll() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Every voice will stay silent until its firmware is imported again.")
        }
        .sheet(isPresented: $showLicenses) { LicensesView() }
        .onAppear(perform: choosePreviewVoice)
        .onChange(of: model.voices.map { $0.label ?? "" }) { _ in choosePreviewVoice() }
    }

    /// The preview speaks an imported voice: the one chosen while it is imported, else the first imported.
    private func choosePreviewVoice() {
        let imported = model.voices.filter { $0.label != nil }.map { $0.voice.key }
        if !imported.contains(previewVoice), let first = imported.first { previewVoice = first }
    }

    private func planWords(_ plan: FirmwareImport.Plan?) -> String {
        guard let plan = plan else { return "" }
        var s = plan.found.map { "\($0.label), from \($0.from)" }.joined(separator: ". ") + "."
        if !plan.notes.isEmpty { s += " " + plan.notes.joined(separator: " ") }
        if plan.found.contains(where: { $0.kind == FirmwareImport.spanish }) {
            s += " Preparing the Spanish unit takes a while."
        }
        return s
    }
}

struct LicensesView: View {
    @Environment(\.dismiss) private var dismiss

    private var text: String {
        guard let dir = Bundle.main.url(forResource: "licenses", withExtension: nil),
              let files = try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil)
        else { return "" }
        return files.sorted { $0.lastPathComponent < $1.lastPathComponent }.compactMap { url in
            (try? String(contentsOf: url, encoding: .utf8)).map {
                url.deletingPathExtension().lastPathComponent + "\n\n" + $0
            }
        }.joined(separator: "\n\n")
    }

    var body: some View {
        NavigationStack {
            ScrollView {
                Text("SSI-263 Speech is MIT-licensed, and its source is at github.com/tgeczy/ssi263-speech. It " +
                     "carries no firmware. MAME's CPU cores keep their BSD-3-Clause licences.\n\n" + text)
                    .textSelection(.enabled)
                    .padding()
            }
            .navigationTitle("Licenses and source")
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
        }
    }
}

// ---- Voice settings -----------------------------------------------------------------------------------------------

/// A 0...max setting as a slider that says its value, and steps by one with VoiceOver's adjust.
struct SettingSlider: View {
    let title: String
    @Binding var value: Int
    let range: ClosedRange<Int>
    var describe: (Int) -> String = { "\($0)" }

    var body: some View {
        VStack(alignment: .leading) {
            Text("\(title): \(describe(value))").accessibilityHidden(true)
            Slider(value: Binding(get: { Double(value) }, set: { value = Int($0.rounded()) }),
                   in: Double(range.lowerBound)...Double(range.upperBound), step: 1)
                .accessibilityLabel(title)
                .accessibilityValue(describe(value))
                .accessibilityAdjustableAction { dir in
                    switch dir {
                    case .increment: value = min(value + 1, range.upperBound)
                    case .decrement: value = max(value - 1, range.lowerBound)
                    @unknown default: break
                    }
                }
        }
    }
}

struct VoiceSettingsView: View {
    private static let store = SsiShared.defaults()
    @AppStorage(SsiShared.Key.pauseMode, store: store) private var pauseMode = 1
    @AppStorage(SsiShared.Key.rate, store: store) private var rate = 50
    @AppStorage(SsiShared.Key.pitch, store: store) private var pitch = 50
    @AppStorage(SsiShared.Key.volume, store: store) private var volume = SsiShared.defaultVolume
    @AppStorage(SsiShared.Key.sampleRate, store: store) private var sampleRate = SsiShared.outputRate
    @AppStorage(SsiShared.Key.inflection, store: store) private var inflection = true
    @AppStorage(SsiShared.Key.tone, store: store) private var tone = 7
    @AppStorage(SsiShared.Key.shortPauses, store: store) private var shortPauses = true
    @AppStorage(SsiShared.Key.numbers, store: store) private var numbers = true
    @AppStorage(SsiShared.Key.runAhead, store: store) private var runAhead = false
    @AppStorage(SsiShared.Key.soTone, store: store) private var soTone = 8
    @AppStorage(SsiShared.Key.soJoin, store: store) private var soJoin = true
    @AppStorage(SsiShared.Key.soShortPauses, store: store) private var soShortPauses = true
    @AppStorage(SsiShared.Key.logRequests, store: store) private var logRequests = false

    private static let letters = (0..<26).map { String(UnicodeScalar(UInt8(65 + $0))) }

    var body: some View {
        Form {
            Section {
                Picker("Pauses between VoiceOver's phrases", selection: $pauseMode) {
                    Text("Off").tag(0)
                    Text("Short").tag(1)
                    Text("Long").tag(2)
                }
            } header: {
                Text("VoiceOver").accessibilityAddTraits(.isHeader)
            } footer: {
                Text("The pauses VoiceOver asks for between an item's name, its kind and its hint: long as asked, " +
                     "short at half, or off.")
            }

            Section {
                SettingSlider(title: "Rate", value: $rate, range: 0...100)
                SettingSlider(title: "Pitch", value: $pitch, range: 0...100)
                SettingSlider(title: "Volume", value: $volume, range: 0...200)
                Picker("Sample rate", selection: $sampleRate) {
                    Text("11 kHz, like the unit's own speaker").tag(11025)
                    Text("22 kHz (recommended)").tag(22050)
                }
                Toggle("Voice inflection", isOn: $inflection)
            } header: {
                Text("Every voice").accessibilityAddTraits(.isHeader)
            } footer: {
                Text("Rate and pitch at 50 are each unit's factory settings; VoiceOver's own rate and pitch come on " +
                     "top. Inflection and the sample rate restart the units on the next utterance.")
            }

            Section {
                Stepper("Tone: \(tone)", value: $tone, in: 0...26)
                Toggle("Short pauses", isOn: $shortPauses)
                Toggle("Read numbers as words", isOn: $numbers)
                Toggle("Run the unit ahead (experimental)", isOn: $runAhead)
            } header: {
                Text("Braille Lite").accessibilityAddTraits(.isHeader)
            }

            Section {
                Picker("Tone", selection: $soTone) {
                    ForEach(0..<26, id: \.self) { Text(Self.letters[$0]).tag($0) }
                }
                Toggle("Join phrases", isOn: $soJoin)
                Toggle("Shorten pauses between sentences", isOn: $soShortPauses)
            } header: {
                Text("Speak-Out").accessibilityAddTraits(.isHeader)
            }

            Section {
                Toggle("Log each request's text, for finding problems", isOn: $logRequests)
            } header: {
                Text("Troubleshooting").accessibilityAddTraits(.isHeader)
            }
        }
        .navigationTitle("Voice settings")
    }
}
