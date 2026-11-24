# SSI-263 Speech for macOS and iOS (in progress)

A system speech voice for VoiceOver and every app on the Mac, iPhone and iPad, speaking with the same emulated
SSI-263 hardware as the NVDA add-ons, the SAPI engine, the Linux module and the Android app, with the same PCM for
the same text and settings. The Android app is the blueprint (`src/platforms/android`), and TGSpeechBox's Apple app
is the model for the Xcode project, the speech extension and its VoiceOver pauses.

The apps ship hollow: no firmware at all, only this project's own MIT code and MAME's BSD-3-Clause cores. Each user
imports the firmware they own, recognised by content (SHA-256), never by name.

## Voices and languages

Ten voices, each speaking one language, offered to the system only once their firmware is imported: the Braille
Lite 2000 in English, Spanish, German and Italian, the Slovak Braille 'n Speak 2000, the Speak-Out, the Mockingboard
(1.1 and early) and the two Accents; with them GW Micro's Sounding Board, Automated Functions' SmarTalk, the Artic
SynPhonix and Speech Systems' Super Voice (below). Then the Braille Lite 2000's language updates: one voice per language the
release table names a voice for (`src/csrc/blazie/bl_languages.txt`: Hungarian, Turkish, Polish and 14 more), read
from the library (`blv_release_*`, `ssa_release_voice`), never listed in Swift. Each is offered under the system's
own locale for its language (hu-HU; Arabic as ar-001, Norwegian as nb-NO, Portuguese as pt-PT beside Brazil's
pt-BR), and named in English ("Braille Lite 2000 (Russian)", the table's `vname`). The Setup list shows each
voice's file (`BL2HUN.BNS — not imported`); the languages wait behind "Show all Braille Lite importable languages (17,
N imported)", imported or not. One import takes them all from the NVDA add-on too (`_ssi263_blazie/bl2000/`). A release no voice speaks yet (the betas, Japanese, the 640s) is still
imported, kept under `units/` for the Blazie Engineering unit tab; Setup names those behind a button of their own, "Show
the Blazie Engineering unit screen's units (13, N imported)" (the library's rows with no language and no voice), and
an import that brings one alone says it is no voice ("Note: this one cannot be used as a voice; it runs on the unit
screen", `FirmwareImport.doneMessage`, Android's). A zip is read in every folder, each entry by its content alone (an
archive of one's own, the emulator's download, the firmware collection, the add-on inside any of them); a zip in it
is opened too, one level, and a zip in that one is named, never dropped in silence. An entry of the source is read up
to 32 MB (a zip in it), anything else over 4 MB is skipped. Last, after the release voices so no voice before it
moves (`ssa_engine.h`'s `SSA_SOUNDINGBOARD`, 27 of 31): GW Micro's Sounding Board, the Speak-Out's voice on the
card's own DOS drivers (`src/csrc/soundingboard`, on the Accent-mini's pc86), imported from `sb.zip`, from the 0.8.5
add-on (`_ssi263_speakout/`, beside the Speak-Out's HEX) or as its `SBLOAD.COM` and `SB.COM` chosen together (several
loose files are judged as one source): the voice needs both, so one alone is refused with why. It is always shown in
the firmware list, listed right under the Speak-Out as the NVDA driver lists it (its index unchanged), and takes the Speak-Out's settings. After it (0.9.0, `SSA_SMARTALK`, 28 of 31): Automated
Functions' SmarTalk, its own DOS text-to-speech `S_TALK.EXE` 1.21 on the same pc86 (`src/csrc/smartalk`), known by its
sha256 alone (`ssa_import_smartalk`) and imported as the file, from the disk's zip or from any zip or add-on holding
it; the app never carries it (the Android APK does), and `check_archive_no_firmware.py` refuses it by its sha256.
Setup names the copy to bring by version and size, never where to get it; a file called `S_TALK.EXE` that is not it is
told which copy the voice runs, with its sha256 (`ssp_smartalk_sha256`, the library's own), only at the import (the
name words that alone: the firmware is found by its content). It is checked to speak, nothing prepared; rate, pitch, volume, inflection and "Read numbers as words" apply,
its tone is the program's own. After it (0.9.5, `SSA_SYNPHONIX`, 29 of 31): the Artic SynPhonix, Artic's own DOS
text-to-speech `VEST.COM` 1.60 on the same pc86 (`src/csrc/synphonix`), import only, as the SmarTalk: the repository
holds a copy (its card serial set to 99999999) that the Android APK carries, and the Apple apps never do --
`check_archive_no_firmware.py` refuses it by its sha256 and any other copy by VEST's own banner. Every copy holds its
own card's serial, so no hash can know them: `ssa_import_synphonix` knows VEST 1.60 by its content (its banner and the
serial check the host reads the card's key from), from the person's own copy, a Business Vision or SynPhonix disk, or
the NVDA add-on's `_ssi263_synphonix/`. It is checked to speak, nothing prepared; rate, pitch, volume, inflection and
"Read numbers as words" apply, its tone is the program's own. After it (0.9.5, `SSA_SUPERVOICE`, 30 of 31): Speech
Systems' Super Voice, the Tandy Color Computer cartridge's own text-to-speech `SUPERTOK.BIN` on MAME's 6809
(`src/csrc/supervoice`), import only and carried by no package at all: the person's own copy of the program, the
Color Computer disk image holding it (`SUPERV.DSK`) or a zip holding that, read by the library itself
(`ssa_import_supervoice`, `sv_firmware_from`: the known program by its sha256, the program alone kept). The app names
the files, never where they are kept (`docs/super-voice.md` does). `check_archive_no_firmware.py` refuses the program
and a Color Computer disk image. It is checked to speak, nothing prepared; rate, pitch and volume apply, its
inflection, tone and numbers are the program's own (it reads money, decimals and numbers up to the trillions itself).
The 6809's BSD-3-Clause notice is among the app's licenses. Languages work as in TGSpeechBox's speech extension, with Android's one locale
per voice: the units have no dialect firmware, so the English voices are en-US only and the Spanish Braille Lite es-ES
only (0.8's first builds also offered en-GB, en-AU, en-CA and es-MX copies; their identifiers still name the voice). The locale ends the voice's identifier (`com.ssi263speech.braillelite.de-de`), and VoiceOver switches language
by choosing another voice for the next request. `<lang>` elements inside a request are not switched on. "Lock
language" (Voice settings) speaks every request with the voice chosen in the app, whatever the request names, for
readers that always ask for the system's language.

An import leaves a unit already here as it is (0.9.5, Android's rule; `FirmwareImport.leaveAlone`, once on the plan
`FirmwareInstaller.inspect` puts to the person): a unit found is already here when its firmware is the same bytes as
the copy in the unit folder -- each of a two-file voice's files, both kept -- and a Blazie unit's state there is the
one its release makes (`blv_state_check`). Those are taken out of the import and named in a note ("... is already
here, unchanged, so it is left as it is."); when nothing is new there is no confirmation, only an "Already imported"
alert (`FirmwareImport.nothingNew`: "Everything in the-add-on.nvda-addon is already here, unchanged: ... Nothing was
imported."). Importing the NVDA add-on a second time so prepares nothing again. Another release, another card's
`VEST.COM`, or a missing or wrong state is imported as before: the last is the quiet repair.

## The app's pages, as Android's

Setup and Voice settings follow the Android app's pages and words (`SettingsActivity.kt`), with VoiceOver in place of
TalkBack and nothing built in. Setup: the voices, the firmware list (file names, the languages under "Show all"), the
import (a line per unit in the confirmation: how long it takes, and whether it replaces one already here; the device
kept awake while it runs; refused below 16 MB free), "Try it" (with Android's "Export to WAV…": the preview's render written where the person chooses -- the save
panel on the Mac, the file exporter on iOS -- named by `WavExport.swift` as Android's `WavExport.kt` names it,
`speakout-20261018-164533.wav`, `braillelite-de-...`; a Stop during the render, or a cancelled or failed write, leaves
no file), where to choose the voice, the licences. On the Mac, `say` sees the voices too: `say -v "Speak-Out (en-US)"
-o hello.aiff "Hello there."` (or `--file-format=WAVE`) writes the same voice to a file. Voice settings:
the voice and Lock language, rate, pitch, tone, engine volume (135 percent by default), the unit's settings (inflection,
short pauses, numbers, run ahead, line lift, the idle sound, the sample rate), the Speak-Out's, and VoiceOver's pauses.
Left out on purpose: Android's built-in Accent-PC and Mockingboards (the app carries no firmware), its update check (the
App Store's), and 44 kHz (the Audio Unit runs at 22050).

## The Blazie Engineering unit tab

The app's third tab ("Blazie eng. unit" on the tab, the whole name to VoiceOver) runs a Blazie unit on the firmware already imported for the voices: one app, no second firmware
picker (PR 18's standalone emulator was declined for that). Its units are the emulator's own catalog (`emc_list`,
src/apps/blazie/emu_catalog.c, the list every emulator shell reads) over the unit folder: the shipped units under
their flat names and every release on the table by its content, `units/` included -- never a unit without its
firmware. A release unit's first start makes its state by its own recipe (`emc_make_factory`). Each unit's memory is
the app's own (Application Support, "Braille Lite units"), saved when it is switched off or the app is left; the
voices' states in the App Group are only read. The unit runs in real time (`emu_unit.h`: its sound pulled by the
audio engine, its keys pushed as they go down and up through `chords.c`, as the Windows shell). Its braille field is
the Windows and GTK pattern: one read-only element holding the display as Unicode braille, keeping the keyboard (F D
S J K L, the space bar, ; and A the bars, by the keys' places), Alt+Shift+Right and Left the display's bars held.
Whether VoiceOver sends the field to braille alone, and whether its keys reach the unit, are for the testers.

One unit runs at a time: while it runs the unit choice is dimmed and "Switch off" is the only way to another.
"Load programs and files into flash" is the Windows and GTK shells' (`UnitFiles.swift`, on `bl_files_xfer.h`'s
`blx_add_cp`): the unit switched off and saved, the files (.bns, .bas, .txt, .brl, .brf, or any) added to its flash
startup folder in one batch, names it has already replaced only when the person says so, the memory as it was kept as
`<state>.before-import`, the unit switched on again. The Sound section has the GTK shell's idle channel (as the unit,
hiss, whine, silent), how long the channel stays open (off, until the unit clicks off, always), the pop and click, and
the 10 Hz tick (`emu_set_idle`), with its defaults.

While a unit runs, "Restart the unit (keeps its files)" switches it off and on as a real unit: saved (`emu_save`,
which never asks the firmware, so a unit that stopped answering is saved too) and started again from what it saved,
with the tab's settings applied again -- never the factory reset. "Restart in one-handed mode (dot 6 held as it
starts)" and "... two-handed mode (dot 3 held as it starts)" are that restart with the key held for the unit's first
2 s (`emu_hold_at_start`, set before its first render): the firmware says "One-hand mode on" and keeps the mode in its
memory. They are not offered for the Type 'n Speak, whose manual has no such mode. "Double-speed upgrade installed"
(off by default, `emu_set_double_speed`, applied at every start and at once) is Blazie's $99 chip: the unit's own
switch (the status menu, then + (dots 3-4-6), answered y) then doubles its CPU, and BASIC's tones can garble as on a
real unit (the latch race); Restart gets a unit out of anything that stops it.

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

## TestFlight

    python src/platforms/apple/testflight.py build [--bump]      archives for macOS and iOS, made fresh and checked
    python src/platforms/apple/testflight.py upload              exactly those, then processed, for the internal group
    python src/platforms/apple/testflight.py status | crashes

This is TGSpeechBox's `testflight.py`, with its rules kept. The build number is the same on both platforms
(`CURRENT_PROJECT_VERSION` in `project.yml`, and the Audio Unit's version with it), raised by one for each upload. A
failed-looking upload is never retried before `status` shows what arrived. What goes up is the archive `build` checked:
its build numbers, its icon and privacy manifests, and `test/check_archive_no_firmware.py`, which makes sure it
carries no firmware at all (Android's checks, plus Aicom's files). The icon is drawn by `tools/make_icon.swift`
(`tools/make_icon.sh` adds the Mac sizes).

## Test

    sh build_linux.sh                                       the desktop references, on the Mac too
    python src/platforms/android/test/test_android_native.py
    python src/platforms/apple/test/test_apple_core.py [--simulator]
    SSI263_APPLE_TEST_BREAK=1 python src/platforms/apple/test/test_apple_core.py      control: must FAIL
    python src/platforms/apple/test/test_apple_speech.py
    SSI263_APPLE_SPEECH_BREAK=ssml-1|ssml-2|ssml-3|speech-1|speech-2|import|import-2|synphonix|synphonix-import|
        supervoice|supervoice-import python ...                                                                       controls: each FAILs
    sh src/platforms/apple/test/run_swift_tests.sh                                     the import, in Swift
    sh src/platforms/apple/test/test_installer.sh                  the app's import on the real firmware, as the app runs it,
                                                                   then the unit tab's catalog and two units started
    SSI263_UNIT_BREAK=factory|restart sh src/platforms/apple/test/test_installer.sh     controls: each FAILs
    sh src/platforms/apple/test/test_extension.sh                  the speech extension as the system drives it
    SSI263_EXTENSION_BREAK=silent|whine|stop sh src/platforms/apple/test/test_extension.sh   controls: each FAILs
    SSI263_SANITIZE=thread sh .../test_extension.sh   under the thread sanitizer (Xcode 27): no race; with =stop, #56's
    SSI263_IMPORT_BREAK=1|unopened|state|state-sizes|speakout|mockingboard|accent|release|soundingboard|smartalk|smartalkname|synphonix|supervoice|same|export|exportcancel sh .../run_swift_tests.sh   controls

`test_apple_core.py` links Android's host-side program (`test_android_native.c`) against each runnable slice's
`libssi263core.a`, the library the apps link, and requires every case's samples and PCM hash to be the desktop
program's, along with the Accent-PC's front-end text. `test_android_native.py` holds that desktop program to the
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
- **The SynPhonix:** the repository's `VEST.COM`, staged into the test's own data folder (never the app's), speaks
  after the SmarTalk (`synphonix` drops the imported copy, so it must FAIL). "Spoke" means the request with no pauses
  made samples and no request had an error.
- **The Super Voice:** with the person's `SUPERTOK.BIN` in `firmware/speech-systems-super-voice/` (never committed),
  staged into the test's data folder, it speaks after the SynPhonix (`supervoice` stages a copy with one byte changed,
  which the host refuses); its program is known from itself, a disk laid out around it and the real `SUPERV.DSK`, the
  program alone written, and a changed program is not (`supervoice-import`: ssa_import_break 9).
- **Import:** each of Aicom's files is known by its sha256 under any name, but not with one byte changed; `VEST.COM`
  is known by its content, a copy with another card's serial as well, and written as it came (`synphonix-import`
  never knows it); a release
  with no shipped voice (a release voice's, and one no voice speaks) comes back as `SSP_FW_RELEASE` plus its row.
- **The release voices:** every one whose firmware is in the firmware folder (`release_voices.py`: found by content,
  its state made once by the desktop library and kept in `build/apple/test/release-states` only while its sha256 is
  the table's) speaks each request as every voice does.

`run_swift_tests.sh` compiles the app's import (`FirmwareImport.swift`, `ZipReader.swift`) with
`FirmwareImportTests.swift`: every case of Android's `FirmwareImportTest.kt` (any folder of a zip, a zip in it and one
deeper said, the emulator's download and the firmware collection, the add-on, update programs, only the releases on the list, never a state, the Speak-Out's HEX, the words), the Accents' files (the
Accent add-on's `accent-sa/` ROMs and driver, loose and renamed), each zip form the reader takes (stored, deflated,
the sizes in a data descriptor), and zips made by `ditto` (Finder's Compress), Info-ZIP's `zip` and Python's
`zipfile`. `SSI263_IMPORT_BREAK=1` (a zip's top alone) fails 39 of its cases, `=unopened` the zip in a zip,
`=smartalk` the SmarTalk's four (its file alone, in the add-on, in its disk's zip, under another name), `=smartalkname` the words for
another `S_TALK.EXE`, `=synphonix` the SynPhonix's two (`VEST.COM` under any name and serial, the add-on's), `=supervoice` the Super
Voice's two (its program, disk and zip; its disk again with the program here), `=same` the four
leave-alone cases (the add-on again, only the new unit, a two-file voice, a unit-tab release; another release and a
missing or wrong state are imported again under it, as they should be).
`test_installer.sh` imports the repository's `S_TALK.EXE` alone and in a zip, and refuses a copy
with one byte changed, naming the file's own sha256 and size; the repository's `VEST.COM`, a copy with another card's
serial and the add-on's, each heard and kept as it came (`SSI263_IMPORT_BREAK=synphonix` fails all three); the
Super Voice's program, real disk and zip, each heard and only the program kept, and the zip again already here
(`=supervoice` fails those seven lines); each
unit imported again is already here, through the real `blv_state_check`, and the Hungarian with no state or a wrong
one is prepared again (`=same` fails the six already-here lines); it also judges a real Blazie firmware collection zip when `FIRMWARE_COLLECTION` (paths.local) names
one: every Blazie voice and unit-tab release in it, each once, the programs left out.

On the Mac, `test_android_native.py` runs every block but the Accent-PC's, whose reference is the NVDA driver on the
Windows DLLs: `SSI263_ANDROID_TEST_ONLY=bl,ra,num,so,mini,st,import`.

## Files

| File | What it does |
|---|---|
| `build_apple.sh` | SSI263Core: the static libraries for each slice, lipo'd per platform, and the XCFramework with its headers |
| `core/ssp_ssml.h`, `ssp_ssml.c` | VoiceOver's SSML: the segments and their pauses at the Pause mode (off, short, long), as TGSpeechBox's speech extension makes them, and the prosody's rate, pitch and volume (the rotor's decibels) |
| `core/ssp_speech.h`, `ssp_speech.c` | A request spoken segment by segment through Android's `ssa_engine`, each segment's PCM unchanged, the pauses' zeros between them, pulled block by block; a stop from any thread |
| `core/ssp_import.h`, `ssp_import.c` | Every firmware the apps import: Aicom's files by sha256 (Android carries them built in), then Android's judgement of the Braille Lite's and the Speak-Out's |
| `app/FirmwareImport.swift` | Android's `FirmwareImport.kt` in Swift, with the Accents' files: what a source holds (zip layouts, the add-ons, update programs, single files), judged by the native side by content, and the refusals in words |
| `app/ZipReader.swift` | A zip's entries in order from their local headers, as `ZipInputStream` reads them: stored and deflated (Apple's Compression), sizes in the header or a data descriptor |
| `app/UnitCatalog.swift` | The unit tab's units (`emc_list` over the unit folder, present only), each one's memory in the app's own folder, a unit's first state |
| `app/UnitRunner.swift` | One unit running: `emu_create`, its sound through AVAudioEngine (`emu_render`), keys through `chords.c`, the bars, the display, the save |
| `app/UnitKeys.swift` | The unit's keyboard by the keys' places: macOS key codes, iOS HID usages |
| `app/BrailleField.swift` | The braille field: the display as Unicode braille for the screen reader, the keyboard kept, never edited |
| `app/WavExport.swift` | Export to WAV's suggested name (Android's rule) and the WAV itself, the preview's render |
| `app/UnitFiles.swift` | Files from the device into a saved unit's flash (`blx_add_cp`), all or nothing, the state before kept |
| `app/UnitModel.swift`, `app/UnitView.swift` | The tab's state (the chosen unit, start and stop, loading files, volume, quick response, the idle channel) and its screen |
| `testflight.py` | TestFlight: fresh archives for both platforms, checked (build numbers, icon, privacy manifests, no firmware), then uploaded as checked |
| `test/check_archive_no_firmware.py` | An archive, app or .ipa carries no firmware: Android's checks (Blazie images, Intel HEX, states, disk images, the Speak-Out's, the Sounding Board's and the Mockingboards' files, names) and Aicom's files by sha256; its controls |
| `app/PrivacyInfo.xcprivacy`, `extension/PrivacyInfo.xcprivacy` | No tracking, no data collected; UserDefaults only for the App Group's settings the app and its extension share (1C8F.1), and file attributes (FileManager.attributesOfItem: the firmware's file protection) in the app's own containers (C617.1). Plain plists, Apple's own layout and no comments: build 1's commented ones were refused as invalid (ITMS-91056) |
| `app/Assets.xcassets`, `tools/make_icon.swift`, `tools/make_icon.sh` | The icon: "263" over a sound wave on deep blue, drawn in code, opaque |
| `test/test_apple_core.py` | Each runnable slice against the desktop program, byte for byte, with its control |
| `test/InstallerTests.swift`, `test_installer.sh` | The app's FirmwareInstaller (inspect, then the Job's commit: the Braille Lite's state made and checked, each unit heard) on the repository's firmware against the macos-arm64 slice, in a temporary folder in place of the App Group's; the unit tab's unit running on after the system stops its sound (#60); the controls |
| `test/ExtensionTests.swift`, `test_extension.sh` | The speech extension's Audio Unit made directly, its requests rendered until Complete on the English Braille Lite: a request that makes no sound still hands over a frame (#61), the idle sound kept across a firmware change (#57), a stop waiting while a firmware change frees the player (#56); the controls |
| `test/FirmwareImportTests.swift`, `run_swift_tests.sh` | The import's layouts and words with a fake native side, the zip forms and the real tools' zips; the controls |
| `test/test_apple_speech.c`, `.py` | The Apple front end's own C: the SSML and pauses, each voice's requests against their segments' own PCM, a stop, Aicom's files, the releases; the controls |
| `test/release_voices.py` | The release voices' files for the tests: each .BNS found by content, its state made once and checked against the table |
