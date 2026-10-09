# SSI-263 Speech for macOS and iOS (in progress)

A system speech voice for VoiceOver and every app on the Mac, iPhone and iPad, speaking with the same emulated
SSI-263 hardware as the NVDA add-ons, the SAPI engine, the Linux module and the Android app, with the same PCM for
the same text and settings. The Android app is the blueprint (`src/platforms/android`), and TGSpeechBox's Apple app
is the model for the Xcode project, the speech extension and its VoiceOver pauses.

The apps ship hollow: no firmware at all, only this project's own MIT code and MAME's BSD-3-Clause cores. Each user
imports the firmware they own, recognised by content (SHA-256), never by name.

## The native core: SSI263Core

`build_apple.sh` builds the chip, every voice's board, host and voice on MAME's Z180, 8085, V40 and 8086 cores (C++17
interpreters with no JIT, which iOS allows), the voice table (`src/csrc/voices.c`), Android's plain-C front end
(`ssa_engine`, `ssa_map` and `ssa_import`, used unchanged) and the Apple front end's own C (`core/`). These are the
same sources and flags as `build_android.sh`, with `-ffp-contract=off` throughout so the arithmetic is the reference's.
It makes static libraries for each Apple platform:

| Platform | Architectures | Minimum |
|---|---|---|
| macOS | arm64, x86_64 | 13.0 |
| iOS (devices) | arm64 | 16.0 |
| iOS Simulator | arm64, x86_64 | 16.0 |

    sh src/platforms/apple/build_apple.sh              every slice, then build/apple/SSI263Core.xcframework
    sh src/platforms/apple/build_apple.sh macos        one platform only

The output goes under `build/apple/` (gitignored). The apps link it with `-lc++`.

## Test

    sh build_linux.sh                                       the desktop references, on the Mac too
    python src/platforms/android/test/test_android_native.py
    python src/platforms/apple/test/test_apple_core.py [--simulator]
    SSI263_APPLE_TEST_BREAK=1 python src/platforms/apple/test/test_apple_core.py      control: must FAIL
    python src/platforms/apple/test/test_apple_speech.py
    SSI263_APPLE_SPEECH_BREAK=ssml-1|ssml-2|speech-1|speech-2|import python ...       controls: each must FAIL
    sh src/platforms/apple/test/run_swift_tests.sh                                     the import, in Swift
    sh src/platforms/apple/test/test_installer.sh                  the app's import on the real firmware, as the app runs it
    SSI263_IMPORT_BREAK=1|state|speakout|accent sh src/platforms/apple/test/run_swift_tests.sh   controls: must FAIL

`test_apple_core.py` links Android's host-side program (`test_android_native.c`) against each runnable slice's
`libssi263core.a`, the library the apps link, and requires every case's samples and PCM hash to be the desktop
program's, along with the Accent SA's front-end text. `test_android_native.py` holds that desktop program to the
references, so the slices are held to them too. It runs macOS arm64 natively, x86_64 under Rosetta, and, with
`--simulator`, the iOS Simulator's arm64 in a booted simulator (`xcrun simctl spawn`). The device slice is checked
on the device by the app. Its control runs the slices with the request's rate dropped (`ssa_map_break`), so the run
must fail.

`test_apple_speech.py` holds the Apple front end's own C to its rules, on the macos-arm64 slice:
- **SSML:** VoiceOver's SSML is parsed as TGSpeechBox's speech extension parses it. The cases cover iOS 27's `<s>`
  parts, an `800.0ms` break folding into the sentence end before it, the strengths, the 30 ms floor, the 2 s cap, the
  end-of-request pause, a break-only spacer, the entities, the rate, and the volume rotor's decibels.
- **Speaking:** every voice whose files are there speaks each request byte for byte as its segments through
  `ssa_start`/`ssa_pull` directly, with the pauses' zeros between them. A stop holds, and off, short and long differ
  in length.
- **Import:** each of Aicom's files is known by its sha256 under any name, but not with one byte changed.

`run_swift_tests.sh` compiles the app's import (`FirmwareImport.swift`, `ZipReader.swift`) with
`FirmwareImportTests.swift`: every case of Android's `FirmwareImportTest.kt` (the zip layouts, the add-on, update
programs, only the releases on the list, never a state, the Speak-Out's HEX, the words), the Accents' files (the
Accent add-on's `accent-sa/` ROMs and driver, loose and renamed), each zip form the reader takes (stored, deflated,
the sizes in a data descriptor), and zips made by `ditto` (Finder's Compress), Info-ZIP's `zip` and Python's
`zipfile`.

On the Mac, `test_android_native.py` runs every block but the Accent SA's, whose reference is the NVDA driver on the
Windows DLLs: `SSI263_ANDROID_TEST_ONLY=bl,ra,num,so,mini,import`.

## Files

| File | What it does |
|---|---|
| `build_apple.sh` | SSI263Core: the static libraries for each slice, lipo'd per platform, and the XCFramework with its headers |
| `core/ssp_ssml.h`, `ssp_ssml.c` | VoiceOver's SSML: the segments and their pauses at the Pause mode (off, short, long), as TGSpeechBox's speech extension makes them, and the prosody's rate, pitch and volume (the rotor's decibels) |
| `core/ssp_speech.h`, `ssp_speech.c` | A request spoken segment by segment through Android's `ssa_engine`, each segment's PCM unchanged, the pauses' zeros between them, pulled block by block; a stop from any thread |
| `core/ssp_import.h`, `ssp_import.c` | Every firmware the apps import: Aicom's files by sha256 (Android carries them built in), then Android's judgement of the Braille Lite's and the Speak-Out's |
| `app/FirmwareImport.swift` | Android's `FirmwareImport.kt` in Swift, with the Accents' files: what a source holds (zip layouts, the add-ons, update programs, single files), judged by the native side by content, and the refusals in words |
| `app/ZipReader.swift` | A zip's entries in order from their local headers, as `ZipInputStream` reads them: stored and deflated (Apple's Compression), sizes in the header or a data descriptor |
| `test/test_apple_core.py` | Each runnable slice against the desktop program, byte for byte, with its control |
| `test/InstallerTests.swift`, `test_installer.sh` | The app's FirmwareInstaller (inspect, then the Job's commit: the Braille Lite's state made and checked, each unit heard) on the repository's firmware against the macos-arm64 slice, in a temporary folder in place of the App Group's |
| `test/FirmwareImportTests.swift`, `run_swift_tests.sh` | The import's layouts and words with a fake native side, the zip forms and the real tools' zips; the controls |
| `test/test_apple_speech.c`, `.py` | The Apple front end's own C: the SSML and pauses, each voice's requests against their segments' own PCM, a stop, Aicom's files; the controls |
