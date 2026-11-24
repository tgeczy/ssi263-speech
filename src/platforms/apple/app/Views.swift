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
            NavigationStack { UnitView() }
                // every Blazie Engineering unit, the 640 and the Braille 'n Speak 2000 too (Tomi, 2026-10-18:
                // "Blazie eng. unit" to see, the whole name to hear)
                .tabItem {
                    Label { Text("Blazie eng. unit").accessibilityLabel("Blazie Engineering unit") }
                        icon: { Image(systemName: "hand.point.up.braille") }
                }
        }
    }
}

// ---- Setup ------------------------------------------------------------------------------------------------------------

struct SetupView: View {
    @EnvironmentObject var model: AppModel
    @State private var picking = false
    @State private var confirmRemove = false
    @State private var showLicenses = false
    @State private var allLanguages = false
    @State private var allUnits = false
    @AppStorage(SsiShared.Key.voice, store: SsiShared.defaults()) private var previewVoice = "accentsa"
    @State private var text = "Hello there. This is SSI-263 Speech, speaking on Apple. You owe 1,234 dollars."

    var body: some View {
        Form {
            Section {
                Text("Talking hardware of the 1980s, emulated around a model of the Silicon Systems SSI-263 speech " +
                     "chip. Each voice is the device's own firmware, running live: the rules, the number reading " +
                     "and the inflection are its own, and nothing is recorded. They are the same voices as the NVDA " +
                     "add-ons, byte for byte.")
                Text("The voices: the Blazie Braille Lite 2000 in speech-box mode, its own firmware on an emulated " +
                     "Z180, in English, Spanish, German and Italian, and with its language updates in many more " +
                     "(the list below); the Blazie Braille 'n Speak 2000 the same way, in Slovak; the GW Micro " +
                     "Speak-Out, its own firmware on an emulated V40, and GW Micro's Sounding Board, the same voice " +
                     "on the card's own DOS drivers; Automated Functions' SmarTalk, its own DOS text-to-speech on " +
                     "an emulated PC, in English; the Artic SynPhonix, a speech card inside the PC, Artic's own DOS " +
                     "text-to-speech VEST on an emulated PC, in English; the Aicom Accent-PC and Accent-mini; and the " +
                     "Sweet Micro Systems Mockingboard, the Apple II's speech card, on an emulated 6502. Each " +
                     "language is offered to VoiceOver and the system once its unit is imported.")
            }

            Section {
                // the release voices (the Braille Lite's many languages) only when asked for, imported or not:
                // seventeen more lines are a wall to read past (Tomi); the button counts the imported ones
                let languages = model.voices.filter { $0.voice.release >= 0 }
                ForEach(model.voices.filter { $0.voice.release < 0 || allLanguages }) { v in
                    VStack(alignment: .leading) {
                        Text(v.voice.listName).font(.headline)
                        Text(v.voice.bring + " — " + (v.label ?? "not imported")).font(.subheadline)
                    }
                    .accessibilityElement(children: .combine)
                }
                if !languages.isEmpty {
                    let here = languages.filter { $0.label != nil }.count
                    Button(allLanguages ? "Hide the Braille Lite languages"
                           : "Show all Braille Lite importable languages (\(languages.count), \(here) imported)") {
                        allLanguages.toggle()
                    }
                }
                // the releases no voice speaks, for the unit tab alone, behind their own button the same way (Tomi,
                // 2026-10-18), so an import of a whole collection shows what came in for it
                if allUnits {
                    ForEach(model.units) { u in
                        Text("\(u.name): \(u.bring) — " + (u.label.map { "imported (\($0))" } ?? "not imported"))
                    }
                }
                if !model.units.isEmpty {
                    let here = model.units.filter { $0.label != nil }.count
                    Button(allUnits ? "Hide the Blazie Engineering unit screen's units"
                           : "Show the Blazie Engineering unit screen's units (\(model.units.count), \(here) imported)") {
                        allUnits.toggle()
                    }
                }
            } header: {
                Text("Voices").accessibilityAddTraits(.isHeader)
            } footer: {
                Text("This app carries no firmware. Each voice speaks once you import the firmware you own: the " +
                     "Braille Lite 2000's and its language updates, GW Micro's Speak-Out's, GW Micro's sb.zip for " +
                     "the Sounding Board (or its SBLOAD.COM and SB.COM together: the voice needs both), the " +
                     "SmarTalk's S_TALK.EXE 1.21, from Automated Functions' SmarTalk V1.21 disk (36,882 bytes), " +
                     "the SynPhonix's VEST.COM, from a Business Vision or SynPhonix disk, or the NVDA add-on that " +
                     "has it; your own copy, whatever its serial; Aicom's " +
                     "for the Accent-PC and Accent-mini, and " +
                     "for the Mockingboard, your own copy of the Mockingboard Developers Toolkit disk.")
            }

            Section {
                if model.importing {
                    VStack(alignment: .leading) {
                        Text(model.step)
                        ProgressView(value: model.progress)
                            .accessibilityLabel("Import progress")
                            .accessibilityValue("\(Int(model.progress * 100)) percent")
                    }
                    Text("Keep the app open until it finishes.")
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
                Text("Choose firmware files you own, update programs, disk images, or .zip or .nvda-addon packages " +
                     "containing them, one or several at once. Each file is known by its contents, whatever its " +
                     "name, and a zip may hold any of them in any of its folders, even in a zip inside it, so an " +
                     "archive of your own imports in one go. This device then prepares each Braille Lite unit once: a few seconds for most, about a " +
                     "minute for Spanish; the others are checked in a moment. The files stay in this app's own " +
                     "storage and never leave this device.")
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
                    Button("Export to WAV…") { model.export(text, voiceKey: previewVoice) }
                        .disabled(model.exporting)
                    Button("Stop") { model.stopPreview() }
                }
                if !model.previewStatus.isEmpty { Text(model.previewStatus) }
            } header: {
                Text("Try it").accessibilityAddTraits(.isHeader)
            } footer: {
                #if os(macOS)
                Text("Export to WAV saves the text, spoken as Speak would say it, as a sound file where you choose. " +
                     "The say command in Terminal can too, with any imported voice: say -v \"Speak-Out (en-US)\" " +
                     "-o hello.aiff \"Hello there.\" (say -v ? lists them).")
                #else
                Text("Export to WAV saves the text, spoken as Speak would say it, as a sound file where you choose.")
                #endif
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
        .confirmationDialog("Import the firmware?", isPresented: $model.askingToImport, titleVisibility: .visible) {
            Button("Import") { model.confirm() }
            Button("Cancel", role: .cancel) { model.cancelImport() }
        } message: {
            Text(model.pending.map(FirmwareInstaller.confirmation) ?? "")
        }
        .confirmationDialog("Remove all firmware?", isPresented: $confirmRemove, titleVisibility: .visible) {
            Button("Remove", role: .destructive) { model.removeAll() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Every voice will stay silent until its firmware is imported again.")
        }
        .sheet(isPresented: $showLicenses) { LicensesView() }
        #if os(iOS)
        .fileExporter(isPresented: $model.showExporter, document: model.exportDocument, contentType: .wav,
                      defaultFilename: String(model.exportName.dropLast(4))) { model.exportFinished($0) }
        .onChange(of: model.showExporter) { shown in if !shown { model.exporterClosed() } }
        #endif
        .onAppear(perform: choosePreviewVoice)
        .onChange(of: model.voices.map { $0.label ?? "" }) { _ in choosePreviewVoice() }
    }

    /// The preview speaks an imported voice: the one chosen while it is imported, else the first imported.
    private func choosePreviewVoice() {
        let imported = model.voices.filter { $0.label != nil }.map { $0.voice.key }
        if !imported.contains(previewVoice), let first = imported.first { previewVoice = first }
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
                Text("This app is free software under the MIT License; its emulated processors are MAME's, under " +
                     "their BSD-3-Clause licenses, and the Mockingboard's 6502 is Mike Chambers' Fake6502 (public " +
                     "domain), by way of Jayson Smith's EchoTalk (BSD-3-Clause). Its source is at " +
                     "github.com/tgeczy/ssi263-speech. The app carries no firmware: the copy you import is its " +
                     "makers', is not covered by those licenses, and never leaves this device.\n\n" + text)
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
    @AppStorage(SsiShared.Key.lineLift, store: store) private var lineLift = false
    @AppStorage(SsiShared.Key.lockLanguage, store: store) private var lockLanguage = false
    @AppStorage(SsiShared.Key.voice, store: store) private var chosenVoice = "accentsa"
    @AppStorage(SsiShared.Key.soTone, store: store) private var soTone = 8
    @AppStorage(SsiShared.Key.soJoin, store: store) private var soJoin = true
    @AppStorage(SsiShared.Key.soShortPauses, store: store) private var soShortPauses = true
    @AppStorage(SsiShared.Key.logRequests, store: store) private var logRequests = false

    @AppStorage(SsiShared.Key.whine, store: store) private var whine = 0

    private static let letters = (0..<26).map { String(UnicodeScalar(UInt8(65 + $0))) }

    /// The voices that can speak now: those imported (the app carries none).
    private var imported: [SsiShared.Voice] { SsiShared.voices.filter { SsiShared.installed($0) } }

    var body: some View {
        Form {
            Section {
                if imported.isEmpty {
                    Text("Import a voice's firmware in Setup first.")
                } else {
                    Picker("Voice", selection: $chosenVoice) {
                        ForEach(imported, id: \.key) { Text($0.name).tag($0.key) }
                    }
                }
                Toggle("Lock language", isOn: $lockLanguage)
            } header: {
                Text("Voice").accessibilityAddTraits(.isHeader)
            } footer: {
                Text("Each voice speaks its own language, and VoiceOver uses the voice you chose in its own " +
                     "settings for a text's language. Lock language speaks everything with the voice chosen here " +
                     "instead, whatever the screen reader asks for: for readers that always ask for the system's " +
                     "language. The voice chosen here is also the one Try it speaks with.")
            }

            Section {
                SettingSlider(title: "Speech rate", value: $rate, range: 0...100) {
                    $0 == 50 ? "50, the unit's factory rate" : "\($0)"
                }
            } header: {
                Text("Rate").accessibilityAddTraits(.isHeader)
            } footer: {
                Text("The NVDA add-ons' scale: 50 is the unit's factory rate. The rate VoiceOver or an app asks for " +
                     "is applied on top.")
            }

            Section {
                SettingSlider(title: "Pitch", value: $pitch, range: 0...100) {
                    $0 == 50 ? "50, the unit's factory pitch" : "\($0)"
                }
            } header: {
                Text("Pitch").accessibilityAddTraits(.isHeader)
            } footer: {
                Text("50 is the unit's factory pitch; an app's pitch is applied on top, such as VoiceOver's higher " +
                     "pitch for capital letters.")
            }

            Section {
                SettingSlider(title: "Tone", value: $tone, range: 0...26) { $0 == 7 ? "7, factory" : "\($0)" }
            } header: {
                Text("Tone").accessibilityAddTraits(.isHeader)
            } footer: {
                Text("The Blazie units' tone setting, 0 to 26. 7 is the factory tone. The Speak-Out has its own, " +
                     "below; the SmarTalk and the SynPhonix keep their programs' own, and the Accents and the " +
                     "Mockingboards have none.")
            }

            Section {
                SettingSlider(title: "Engine volume", value: $volume, range: 0...200) {
                    $0 == SsiShared.defaultVolume ? "\($0) percent, default" : "\($0) percent"
                }
            } header: {
                Text("Volume").accessibilityAddTraits(.isHeader)
            } footer: {
                Text("The engine's own level, for every voice; the system's volume and VoiceOver's still apply. 100 " +
                     "percent is the desktop voices' level; the default, \(SsiShared.defaultVolume), sits beside " +
                     "VoiceOver's own sounds. Above about 140 the loudest syllables can clip.")
            }

            Section {
                Toggle("Voice inflection", isOn: $inflection)
                Text("The unit's own intonation. Off, questions stay flat: the Braille Lite's status-menu setting, " +
                     "and the Accent-PC's, the SmarTalk's and the SynPhonix's monotone.").font(.footnote)
                Toggle("Short pauses", isOn: $shortPauses)
                Text("The Blazie units only: sentences packed onto one line from the second on, as the NVDA " +
                     "add-on's default.").font(.footnote)
                Toggle("Read numbers as words", isOn: $numbers)
                Text("The English and Spanish Braille Lite, the Mockingboards, the SmarTalk and the SynPhonix, on " +
                     "by default, as " +
                     "the NVDA add-on's custom number processing: 1,234,567 as one number, read in words, and the " +
                     "Spanish voice in Spain's way (1.234.567, and 3,5 as tres coma cinco). Off, the unit's own " +
                     "firmware reads them (the Mockingboard's digit by digit, the SmarTalk's digit by digit from " +
                     "three digits on, the SynPhonix's every digit one by one). The Accents keep their own add-on's number processing on; the German, " +
                     "Italian and Slovak units always read their own, as in the NVDA add-on.").font(.footnote)
                Toggle("Run the unit ahead (experimental: with short pauses)", isOn: $runAhead)
                Text("The English and Spanish Braille Lite only, experimental and off by default, as in the NVDA " +
                     "add-on: the unit runs ahead of its speech chip, so long text starts sooner. It needs short " +
                     "pauses on.").font(.footnote)
                Toggle("Lift line starts (as note-taking mode)", isOn: $lineLift)
                Text("The English Braille Lite only, off by default, as in the NVDA add-on: like the unit when you " +
                     "move by line, the start of a line spoken after you interrupt speech, or after a pause, slides " +
                     "up in pitch. Reading on is never lifted.").font(.footnote)
                Picker("Idle sound", selection: $whine) {
                    Text("Off").tag(0)
                    Text("Hiss").tag(1)
                    Text("Whine").tag(2)
                }
                Text("The Blazie units only: the faint sound a real unit makes under its speech, hiss at even " +
                     "volumes (the factory setting), whine at odd ones.").font(.footnote)
                Picker("Sample rate", selection: $sampleRate) {
                    Text("11 kHz, like the unit's own speaker").tag(11025)
                    Text("22 kHz (recommended)").tag(22050)
                }
            } header: {
                Text("The unit").accessibilityAddTraits(.isHeader)
            } footer: {
                Text("Inflection, the idle sound and the sample rate restart the unit on the next utterance, which " +
                     "takes a moment.")
            }

            Section {
                SettingSlider(title: "Speak-Out tone", value: $soTone, range: 0...25) {
                    Self.letters[$0] + ($0 == 8 ? ", the box's own" : "")
                }
                Toggle("Join phrases (fewer pauses between words)", isOn: $soJoin)
                Toggle("Shorten pauses between sentences", isOn: $soShortPauses)
            } header: {
                Text("The Speak-Out").accessibilityAddTraits(.isHeader)
            } footer: {
                Text("The Speak-Out's tones A to Z, as its NVDA add-on offers them; I is the box's own. Joining " +
                     "phrases and shorter pauses are on by default, as in its NVDA add-on. The Sounding Board " +
                     "speaks with the Speak-Out's settings: its drivers' front end is the same.")
            }

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
                Toggle("Log each request's text, for finding problems", isOn: $logRequests)
            } header: {
                Text("Troubleshooting").accessibilityAddTraits(.isHeader)
            }
        }
        .navigationTitle("Voice settings")
    }
}

/// A rendered WAV, as the iOS file exporter takes it.
struct WavDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.wav] }
    let data: Data
    init(data: Data) { self.data = data }
    init(configuration: ReadConfiguration) throws { data = configuration.file.regularFileContents ?? Data() }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { FileWrapper(regularFileWithContents: data) }
}
