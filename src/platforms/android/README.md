# SSI-263 Speech for Android: every voice of the NVDA add-ons

An Android text-to-speech engine -- a system voice for TalkBack and every other app -- that speaks with emulated
SSI-263 hardware, with every voice the NVDA add-ons have (Tomi, 0.7.5: "all voices, no exceptions"):

| Voice (Android name) | How it comes | Firmware, emulated |
|---|---|---|
| Aicom Accent SA (`en-US-accentsa`) | **built in** (Tomi, 2026-09-30); the default while no Braille Lite is imported | the box's 8085 firmware and dictionary ROMs (`u2`, `u3`, `u4`) on MAME's 8085 |
| Aicom Accent-mini (`en-US-accentmini`) | **built in** (0.7.5), when the build has `src/csrc/accentmini` | its DOS driver `SPKEMS.DVC` on an emulated PC, MAME's 8086 |
| Braille Lite 2000, English (`en-US-braillelite`) | **imported** by the user | the unit's June 2003 firmware on MAME's Z180 |
| Braille Lite 2000, Spanish (`es-ES-braillelite`) | **imported** by the user | ONCE's September 2000 firmware on MAME's Z180 |
| GW Micro Speak-Out (`en-US-speakout`) | **imported** by the user (0.7.5): GW Micro's `SPEAKOUT.HEX` | its V40 firmware on MAME's V40 |
| Mockingboard (Sweet Micro Systems) (`en-US-mockingboard`) | **built in** (0.8, the GitHub APK), or imported: `mockingboard-tts-1.1.bin` | Sweet Micro's text-to-speech 1.1 on a 6502 (Fake6502's instructions, via EchoTalk) |
| Mockingboard, early (Sweet Micro Systems) (`en-US-mockingboard-early`) | **built in** the same way, or imported: `mockingboard-tts-early.bin` | the earlier text-to-speech of Mockingboard disk 1, on the same 6502 |

Aicom's files and Sweet Micro's are the only firmware the APK carries (`assets/aicom`, with `firmware/AICOM.txt`;
`assets/sweet-micro`, with Sweet Micro's notice); Blazie's and GW Micro's never ship -- each user imports their own,
through one "Import firmware…" that tells them apart by content.

The native part is the same C as the NVDA add-ons' libraries, the SAPI engine and the Linux speech-dispatcher module
(`src/csrc`), cross-built with the NDK; its voices are made through `src/csrc/voices.c`, the voice table the SAPI
engine speaks through, and its PCM is theirs, byte for byte (the tests below prove it on the desktop and on a phone).

The layout, the settings screen and its accessibility patterns come from outspoken's Android app; the stop handling
also borrows TGSpeechBox's.

## The Accents: built in

The Accent SA's ROMs (u2 64 KB, u3 and u4 32 KB) are read from the APK's assets once and handed to the native side in
memory (`ssa_set_accent_roms`). Each utterance gets a unit of its own, booted as the NVDA driver boots one (2.4 ms on the
A024) and let go afterwards, so an utterance sounds the same whatever came before it: a capital's raised pitch cannot
linger, and a stop needs no flush. The next unit is booted as one is let go. The front end is the NVDA driver's, in C
(`as_voice.c`): its settings commands (ESC R, P, M), currencies, `_clean`, number words (`src/csrc/numwords.c`, the
driver's `ssi263_numwords.py`), the lead trim and the gain.

The Accent-mini's driver (`SPKEMS.DVC`, Aicom's: `firmware/aicom-accent-mini`) is staged beside the ROMs by
`build_android.sh` when `src/csrc/accentmini/am_voice.c` is in the tree, and handed over in memory the same way
(`ssa_set_accent_mini`); its voice is `am_voice.c`, the NVDA Accent driver's "mini" voice in C. It keeps one unit, as
the Speak-Out does, and takes a request's pitch as the driver takes a capital's (below).

The GitHub APK carries Aicom's content and Sweet Micro's (Tomi, 2026-10-10: until a Play Store flavor exists):
`check_apk_no_firmware.py` lets exactly the three ROMs, the Accent-mini's driver and the two Mockingboard files
through, by sha256, and refuses any Blazie or Speak-Out firmware or state and any Apple II disk image.

## The Mockingboards: built in, or imported

The two Mockingboard files (Sweet Micro Systems', `mockingboard-tts-1.1.bin` and `mockingboard-tts-early.bin`) are
staged into the APK's `assets/sweet-micro` by `build_android.sh` from the local, gitignored
`firmware/sweet-micro-mockingboard/` -- never committed -- with Sweet Micro's notice
(`notices/Sweet-Micro-Mockingboard-notice.txt`: not ours, here so the card can speak again, removed if its rights
holders ask, not covered by the MIT license). A file not there at build time leaves its voice import only; the build
goes on. The app reads each once and hands it over in memory (`ssa_set_mockingboard`, which takes only that voice's
file, by `mbh_variant`); an imported copy in the data folder wins over it (`ssa_mockingboard_source`: 1 imported,
2 built in), and removing the imported firmware falls back to the built-in copy.

## The imported firmware: the Braille Lite's and the Speak-Out's, never shipped (and the Mockingboards', if wanted)

The APK carries no Braille Lite or Speak-Out firmware -- this app is the one place it cannot ship -- so each user
imports their own, as outspoken and Panthera take their engine data. Setup's one "Import firmware…" (or `am start -n
com.ssi263speech.tts/.SettingsActivity --es import <path|content: URI>`, which skips the confirm
dialog; `--ez removefirmware true` removes all of it) takes either unit's files, told apart by content (below). A
path under `/sdcard` is refused by scoped storage (EACCES); from adb, hand over the file's MediaStore URI with a grant
(push the file to the phone's Download folder first, and `adb shell content call --method scan_volume --uri
content://media --arg external_primary` if MediaStore has not seen it yet):

    adb shell "content query --uri content://media/external/file --projection _id --where \"_display_name='blt2000.exe'\""
    adb shell am start -n com.ssi263speech.tts/.SettingsActivity -d content://media/external/file/<id> \
        --grant-read-uri-permission --es import content://media/external/file/<id>

It takes:

- a zip, with the firmware at its top or one folder down -- GW Micro's `speakout.zip`, a Braille Lite update -- and
  the NVDA add-on (`.nvda-addon`: `synthDrivers/_ssi263_blazie/`); a zip holding both units' files imports both;
- one file: the Speak-Out's `SPEAKOUT.HEX`; a Mockingboard's `mockingboard-tts-1.1.bin` or
  `mockingboard-tts-early.bin`, or the disk image it comes from; a `.BNS`, or an update
  program (`.exe`/`.com`) holding the image, raw or as a zip behind its code (`blt2000.exe`), also inside a zip.

### The Mockingboards: Sweet Micro's text-to-speech, one file each

`mockingboard-tts-1.1.bin` is the Mockingboard Developers Toolkit disk's six DOS 3.3 files back to back, and
`mockingboard-tts-early.bin` Mockingboard disk 1's four (the earlier text-to-speech, its own rules)
(`tools/mockingboard_firmware.py` makes either from its disk image). Each is known by content alone (`ssa_import.c`):
its sha256 -- `88e1e90f…` and `c7c049b1…`, `src/csrc/mockingboard/mb_host.h`'s `mbh_variant`, the only two files its
host runs. It is written as it is, under its own name; like the Speak-Out's it has no state to make, so it is
checked once (`ssa_probe` says "Hello.") and moved into place, where it wins over the APK's copy. A disk image (a
143,360-byte `.dsk`/`.do` in DOS order or `.po` in ProDOS order, alone or in a zip, at its top or one folder down)
is taken too: `mb_dsk.h`'s `mb_firmware_from_dsk` reads the files out of it -- the toolkit's give the 1.1 file, disk
1's the early one -- and the file is written exactly as a `.bin` import writes it; another disk is refused with the
disk reader's reason ("a Mockingboard disk, but not the text-to-speech version 1.1 this voice runs", "not a DOS 3.3
disk"). Both voices take the app's rate, pitch (a capital's as an offset, at least one of its inflection steps),
volume and "Read numbers as words" (`mb_voice.h`).

**Finding the disks.** The 1.1 voice's disk is Sweet Micro Systems' *Mockingboard Developers Toolkit* (1984). Two
images of it are known, and both hold the same six files (checked 2026-10-09 against every Mockingboard disk in the
archives below):

| Disk image (as archived) | sha256 |
|---|---|
| `Sweet Micro Systems Mockingboard Developers toolkit 1984.dsk` | `7b2930489cfe8952d0338d2d8751cef3bdca004075161050481da8301d0136a2` |
| `Mockingboard - Developer's Toolkit.dsk`, also archived as `MNBTOOLKIT for IIc.DSK` | `15cfb639ce5d9ca38bb650c8cb9662bcaf2b83282b0822fb2bfcd14463817cad` |

The early voice's is Mockingboard disk 1 (`mockingboard1.dsk`, `0d646bd4…`). They are preserved in the public Apple
II archives: the Asimov archive and its mirrors (its Mockingboard folder, under the sound hardware images) and
ReActiveMicro's Mockingboard software downloads. The other Mockingboard disks there -- the demonstration disks, the
1982 Sound and Speech I and Speech Development System disks, `mockingboard2.dsk` (disk 1's program with other
rules) -- are refused. The app itself names no source: it asks for your own copy of the disk.

### The Speak-Out: GW Micro's SPEAKOUT.HEX

Known by content (`app/src/main/cpp/ssa_import.c`), never by name: an Intel HEX file -- every line a record with a
right checksum, ending with its end-of-file record -- whose text is the known firmware's, sha256 `1c6930c8…`
(`firmware/gw-micro-speakout/README.txt`'s; the same hash `check_apk_no_firmware.py` refuses in an APK). The same text
with its lines ending in LF alone, or with a DOS end-of-file mark after it, is the same firmware and is taken; the file
is then written exactly as GW Micro's. Anything else in Intel HEX form is refused in words: a HEX with a damaged record
("an Intel HEX file, but damaged: line 6's checksum or length is wrong"), one cut short, or another HEX ("not GW
Micro's SPEAKOUT.HEX: its contents are not the Speak-Out firmware this app knows"), each followed by "Only GW Micro's
SPEAKOUT.HEX, as it came, can be imported for the Speak-Out." It has no state to make: the box is checked once
(`ssa_probe` says "Hello.") and moved into place as `SPEAKOUT.HEX` beside the Braille Lite's files.

### The Braille Lite: the releases on the list

Files are known by content, never by name (`src/csrc/blazie/bl_firmware.c`): the ROM image (`F3 C3 xx xx FF
"COPYRIGHT"`) anywhere in a file, and only the releases on the list, by the sha256 of their image (Tomi,
2026-09-30) -- each one booted through its state recipe and heard before it was listed:

| Release | Found on | Image sha256 |
|---|---|---|
| English, June 5, 2003 revision | "FS june2003": `blt2000.exe` and its `BL2ENG.BNS` | `ff8f30ec…` |
| English, ONCE's September 20, 2000 revision | ONCE's "braillehablado" disk: `blite2000/BL2ENG.BNS` | `c840112f…` |
| Spanish, ONCE's September 20, 2000 revision | ONCE's "braillehablado" disk: `blite2000/BL2SPA.BNS` | `eedd606e…` |

Any other image is refused in words: a Braille Lite 2000 release not on the list (another country's may lay its
memory out differently, and its recipe is not known), and another Blazie unit's firmware (Braille 'n Speak, Type 'n
Speak, Braille Lite 18 and 40), which `bl_create`'s firmware sites also refuse. A unit's state (786432 bytes, whatever
its name) is never imported, alone or in a zip: the app always makes its own. Picked alone, it is refused with "This
is a state file, not firmware. Please import only firmware files, or zips containing them, with this tool." (Tomi's
words); in a zip beside firmware (the add-on), it is left out and said so. Refusals are shown in a dialog, kept on
the Setup page and announced to TalkBack.

The unit's battery-backed state (it holds what the firmware wrote, so it cannot ship either) is made on the phone
from the firmware (`bl_state.c`): the bns.c runs that made the shipped states, replayed on MAME's Z180, and checked
on the phone against the list's hash for that release (MAME-made: the same bytes on the phone, the desktop and Linux;
the desktop's shipped states, made on z180emu, differ by the recipe's timing and are refused). English takes a few seconds, Spanish about a minute
(1150 million Z180 instructions; 71 s for both on the A024); the progress bar moves every 10 million instructions and
TalkBack hears every fifth of the way, no closer than 5 s apart. Then the unit speaks once (`ssa_probe`) and only then
do the files replace what was there, in device-protected storage. Until then the service reports its languages as
missing data and CheckVoiceData fails.

## Build

    sh build_android.sh                                   # at the repository root
    cd src/platforms/android && ./gradlew assembleDebug assembleRelease
    python src/platforms/android/test/check_apk_no_firmware.py app/build/outputs/apk/release/app-release.apk

`build_android.sh` needs the NDK (`ANDROID_NDK_HOME`, or the newest under `$ANDROID_HOME/ndk`). MAME's Z180, 8085, V40
and 8086 cores are C++17 (no exceptions, no RTTI),
built with the NDK's clang++; libc++ is linked statically into the one `libssi263speech.so` and kept inside it (the
build checks that the library needs nothing beyond libc, libm, libdl and liblog). It writes the libraries to
`app/src/main/jniLibs/<abi>/` and everything else the APK carries to `build/android/assets/` at the repository root
-- the Accent SA's ROMs (from `firmware/aicom-accent-sa`), the Accent-mini's driver (`firmware/aicom-accent-mini`,
when its voice is in the tree), the two Mockingboard files (from the local `firmware/sweet-micro-mockingboard`, each
when it is there), the licences (the project's MIT, Aicom's notice, MAME's BSD-3-Clause notices for the Z180, 8085,
V40 and 8086 cores, and with the Mockingboard's sources in the tree its 6502's credits: Fake6502's `PINNED.txt` and
EchoTalk's BSD-3-Clause, and Sweet Micro's notice with its files). None of that is committed. A developer build may
carry the firmware, by asking: `SSI263_ANDROID_BUNDLE_FIRMWARE=1` (with `SSI263_FIRMWARE`, default `firmware/blazie`,
the Spanish unit there or in `spanish/`); never distribute one. Gradle then needs `-Pssi263BundleFirmware=1` too:
without it, staged firmware stops the build (a leftover once rode into a plain `assembleDebug`); and staged
Mockingboard files without Sweet Micro's notice stop it too. `check_apk_no_firmware.py` looks inside an APK (and any
archive in it): Aicom's three ROMs, the Accent-mini's driver and the two Mockingboard files (in `assets/sweet-micro`
only) pass by sha256 and nothing else does -- a Braille Lite ROM image or a unit's state by content, the Speak-Out's
HEX by its hash and any Intel HEX by content, any Apple II disk image by name or content (143,360 or 232,960 bytes,
a WOZ or 2IMG header), firmware, state or ROM files by name, anything else in `assets/aicom` or `assets/sweet-micro`.
`--control <BL2ENG.BNS> <apk>` adds the firmware under a bland name beside the Aicom ROMs and must fail, as must
`--control <bl2_2003_warm.state>`, `--control <SPEAKOUT.HEX>`, `--control <mockingboard-tts-1.1.bin>` (outside its
folder), `--control <a disk image>`, `--control-aicom-flipped` (u2 with one byte changed),
`--control-mockingboard-flipped` and `--control-no-sweet-notice`; `--synthetic` in place of the APK checks an
APK-like zip made on the spot (run_tests uses it). It also checks the licences: MIT (ours and Casso's), the four MAME
notices, Aicom's and the distribution notice present, Sweet Micro's with its files, and no z180emu, GPL text or
Unicorn anywhere, the native libraries included (`tools/check_no_gpl.py`); `--control-gpl` adds z180emu's GPL text
among the licences and must fail.

Release signing reads `signing.properties` beside `settings.gradle.kts` (gitignored), as outspoken's and
TGSpeechBox's builds: `STORE_FILE`, `STORE_PASSWORD`, `KEY_ALIAS`, `KEY_PASSWORD`. Without it a release stays unsigned.

The app is MIT (Tomi, 0.7: every unit on MAME's cores); MAME's Z180, 8085, V40 and 8086 cores keep their BSD-3-Clause notices,
shipped in the APK. The Setup page's "Licenses and source" shows them all and points to the source on GitHub.

## Test

    python src/platforms/android/test/test_android_native.py              # desktop: the app's C against the references
    sh build_android.sh --test arm64-v8a
    python src/platforms/android/test/test_android_native.py --adb        # ... and on the attached phone
    SSI263_ANDROID_TEST_BREAK=1 python src/platforms/android/test/test_android_native.py   # control: must FAIL
    SSI263_ANDROID_TEST_BREAK=accent-pitch|accent-glide|accent-reuse|accent-step ...      # the Accents': must FAIL
    SSI263_ANDROID_TEST_BREAK=speakout-pitch|speakout-settings|run-ahead|import-hash ...  # the 0.7.5 ones: must FAIL
    SSI263_ANDROID_TEST_BREAK=mockingboard-pitch|mockingboard-numbers|import-dsk ...      # the Mockingboards': must FAIL
    SSI263_ANDROID_TEST_BREAK=mockingboard-import-ignored|mockingboard-builtin-ignored|mockingboard-variant ...
    python src/platforms/android/test/test_volume_headroom.py              # the default volume, every voice
    python src/platforms/android/test/test_device_service.py [--rate 2.0] [--aloud]
                                  [--voice accent|braillelite|speakout|accentmini|mockingboard|both|all]
    python src/platforms/android/test/test_import_native.py               # the import's native part, and the states
    SSI263_IMPORT_TEST_BREAK=1 python src/platforms/android/test/test_import_native.py     # control: must FAIL
    SSI263_IMPORT_TEST_BREAK=hash python src/platforms/android/test/test_import_native.py  # control: must FAIL
    cd src/platforms/android && ./gradlew testDebugUnitTest               # the import's layouts and words, on the JVM
    ./gradlew testDebugUnitTest -Pssi263ImportBreak=1                     # control: the layout cases must FAIL
    ./gradlew testDebugUnitTest -Pssi263ImportBreak=state                 # control: the state cases must FAIL
    ./gradlew testDebugUnitTest -Pssi263ImportBreak=speakout              # control: the Speak-Out cases must FAIL
    ./gradlew testDebugUnitTest -Pssi263ImportBreak=mockingboard          # control: the Mockingboard cases must FAIL

`test_import_native.py` makes every fixture at test time from the files in the firmware folder (`--firmware`, default
`$SSI263_FIRMWARE`, else `firmware/blazie`, with `spanish/`, `tns/` and `once2000/` -- ONCE's September 2000
`BL2ENG.BNS`, its cases skipped when absent): the listed releases written as they came, the list and its labels, an
update program and the 1998 layout, two unknown releases refused, TNSENG.TNS refused, states and noise not firmware;
the states made from the firmware alone must be the listed ones byte for byte (the shipped z180emu-made ones are
refused), speak every case of the Android test within a few samples of the shipped ones, and report their progress every 10 million instructions, steadily. Its controls hold the
wrong chord at the English warm reset, and drop the list (the unknown releases are then taken). The JVM tests
(`app/src/test`) play the native side with a fake and check the zip layouts (top, one folder down, the add-on, an
update program inside a zip, `speakout.zip`, both units in one zip), the refusals' words (unknown releases, other
units, a damaged HEX alone and beside firmware, states alone, in a zip and beside firmware) and the choices between
releases.

`test_android_native.py` also covers 0.7.5's voices and settings. The Braille Lite with run ahead on (its setting,
`blv_set_run_ahead`) against bl_voice running ahead -- and its audio must not be the lockstep's. Its number words
("Read numbers as words", on by default; `blv_set_numbers` with `bl_numbers`), on and off, each on a fresh unit:
"1,234,567", "3.5" and "$12.50" in English, Spain's "1.234.567" and "3,5" in Spanish, against bl_voice with and
without them from a library that has them (`build/win/<arch>/ssi263speech.dll` from `build_ssi263speech.py`, or
`build/linux/libssi263speech.so`; the block is skipped, and said so, without it) -- on and off must differ. The Speak-Out
(`firmware/gw-micro-speakout/SPEAKOUT.HEX`; skipped, and said so, without it) against so_voice driven directly
(`--so-direct`: one kept unit, the settings computed in Python: the request's rate on the slider, the slider's pitch
as the setting and the request's as a capital's offset, the tone, join and short pauses) on 14 cases: rates,
sliders, its own settings, the capitals at 150/75/120 %, a stop and the utterance after it, volume 150, 11 kHz.
The Accent-mini (`firmware/aicom-accent-mini/SPKEMS.DVC`) against am_voice driven directly (`--am-direct`) on 9.
so_voice and am_voice are themselves the NVDA drivers byte for byte (`nvda/tools/so_voice_equiv.py`,
`am_voice_equiv.py`). And the import's native part on the real HEX: as it is, with LF line endings, with a DOS
end-of-file mark, renamed, out of a `speakout.zip` like GW Micro's (its other members not firmware); a digit changed,
the same with its checksum put right (another HEX), one cut short, a text file, all refused; `BL2ENG.BNS` still the
Braille Lite's. Its controls: the Speak-Out's request pitch dropped (`speakout-pitch`), its own settings dropped
(`speakout-settings`), run ahead dropped (`run-ahead`), the number words dropped (`numbers`: always off, as before),
the sha256 check dropped (`import-hash`: another HEX taken).

The Mockingboards (0.8; `firmware/sweet-micro-mockingboard/mockingboard-tts-1.1.bin` and
`mockingboard-tts-early.bin`, never committed: each skipped, and said so, without it), copied into the data folder as
an import leaves them, against mb_voice driven directly on the same file (`--mb-direct`: one kept unit, the settings
computed in Python as the Speak-Out's, the capital's offset at least one of `mbv_pitch_step`'s steps) on 15 cases
each (`mb-`, `mbe-`): rates, sliders, the capitals at 150/75/120 %, the number words on and off (which must differ),
a stop and the utterance after it, volume 150, 11 kHz; and each import's probe. The built-in copies, handed over in
memory as the app does: each voice's "Hello." from its own with no import (`mb-builtin`, `mbe-builtin`: the probe's
PCM), an imported copy winning over it, and neither voice taking the other's file. Its import: each file as it is,
renamed, out of a zip, taken; one byte changed, or cut short, refused. With `MOCKINGBOARD_DISKS` in `paths.local`
(the disk images are never committed; skipped, and said so, without them): the toolkit's `.dsk`, the same in ProDOS
order and in a zip give the 1.1 file; disk 1's the early one (also in ProDOS order, and from a zip's folder); a blank
140 KB disk and `mockingboard2.dsk` are refused with the reader's reason. Its controls: the request's pitch dropped
(`mockingboard-pitch`), the number words dropped (`mockingboard-numbers`), the imported copy ignored
(`mockingboard-import-ignored`), the built-in copies ignored (`mockingboard-builtin-ignored`), either file taken for
either voice (`mockingboard-variant`), the disk image never tried (`import-dsk`); `import-hash` also takes the changed
files. The JVM tests play them with a fake too (each alone, beside the other units one folder down, two folders down
named, both disks in one zip; `-Pssi263ImportBreak=mockingboard` must fail those).

The Braille Lite's reference in both is the desktop library the NVDA add-on and Linux ship (`bl.dll` + `ssi263.dll`
from `build_board.py` / `build_native.py`, or `build/linux/libssi263speech.so`), driven the way `sd_ssi263.c` maps
SSIP: the board on MAME's Z180, as the app's -- a library carrying z180emu is refused as a reference. The Accent SA's is the NVDA Accent add-on's driver itself (`accent_reference.py`: the built
`nvda/dist/accent-build` under the stand-in NVDA of `nvda/tools/fake_nvda_driver_test.py`, its "sa" voice on the
desktop's C host, `accent_sa.dll`), a fresh unit per case, the request's rate and pitch mapped as the app maps them
and the pitch said as a capital's PitchCommand; and its text (currencies, `_clean`, number words) on 22 texts. The
capitals: a request's 150 %, 120 % and 75 % must differ from 100 %, and 100 % again must be byte-identical to the
first. The controls put one bug back each: the request's rate dropped (`1`); its pitch dropped (`accent-pitch`); the
pitch sent as a setting, glided to without snap_pitch (`accent-glide`); one Accent unit kept across utterances
(`accent-reuse`); the plain pitch mapping, 120 % on 100 %'s step (`accent-step`). `test_device_service.py` chooses
each voice through SettingsActivity's `setvoice` hook (and puts the device's choice back) and checks the Accent SA's
sentence and its capitals through the platform TTS against the NVDA driver, the Braille Lite's against bl_voice --
lockstep, then with run ahead on through the `setrunahead` hook (put back afterwards) -- the Speak-Out's sentence
and capitals against so_voice driven directly from the device's own imported HEX, and the Accent-mini's against
am_voice, at the app's default volume. The Braille Lite's reference runs the device's own unit files (its firmware and the
state it made, read with run-as): a 0.7 import makes MAME's state, an earlier one z180emu's, and the two speak a few
samples apart (Spanish's sentence differs), so a reference from the repository's shipped (z180emu-made) state
(`--firmware-from-repo`) fails on a phone imported on 0.7.

## Settings: rate, pitch, volume, and each unit's own

The app's sliders use the NVDA drivers' scales (rate and pitch 0-100, 50 = the unit's factory rate and pitch: the
Braille Lite's 11 and 16, the Accents' 5 and 5, the Speak-Out's 5 and 3; engine volume 0-200, default 150, every
voice: `so_voice` and `am_voice` take up to 200 as `as_voice` and `bl_voice` do, and `test_volume_headroom.py` holds
each voice's loudest line under -0.5 dBFS at the default). Each unit's own settings are its NVDA add-on's, with its
defaults:

| Voice | Settings (Voice settings page) |
|---|---|
| Braille Lite | tone 0-26 (7); short pauses (on); read numbers as words (on: the driver's custom number processing, English and Spain's Spanish); run the unit ahead (**experimental**, off; needs short pauses), as in NVDA, SAPI and Linux; idle sound off/hiss/whine; voice inflection (on) |
| Speak-Out | tone A-Z (I, the box's own); join phrases (on); shorten pauses between sentences (on) |
| Accents | voice inflection (on: full intonation; off: monotone) |

One difference from the NVDA add-on and SAPI is left: the Braille Lite's "custom number processing" (`bl_numbers`)
is off here, as before 0.7.5 (its desktop reference, `bl.dll`, has no number words); the Accents' number words are on,
as in NVDA. An app's request carries rate and pitch as percentages (100 = normal); they go onto SSIP's scale at 50
per doubling, through `sd_ssi263.c`'s `to100`, and on top of the slider (`cpp/ssa_map.c`). Android applies the
request's volume to its own audio track, so the engine's volume is the slider alone.

The Accent SA then takes them as the NVDA Accent driver does: rate 0-100 onto ESC R 0-H, pitch onto ESC P 0-9,
inflection on or off onto ESC M0 or M1, the volume as the gain (volume / 100, `SSA_ACCENT_LEVEL` 100: at the
default it peaks at -2.5 dBFS on `test_volume_headroom.py`'s lines, within a decibel of the Braille Lite). Every
pitch is said as the driver says a capital's -- snap_pitch, then ESC P -- so it jumps rather than glides from a
fresh unit's power-up pitch. TalkBack asks for a capital with a raised pitch on the letter's own request; the
Accent's ten pitch steps are coarse, so a request other than 100 % always moves it at least one step
(`ssa_accent_pitch`): 110-120 % would otherwise land on the slider's own step and go unheard.

The Speak-Out and the Accent-mini keep one unit each and take the request's pitch as their NVDA driver takes a
capital's: the slider's pitch as the setting, the difference as `PitchCommand`'s offset, snapped to for the utterance
and restored after it by the voice itself (`sov_speak`, `amv_speak`), moved at least one of the box's ten steps
(`ssa_speakout_pitch`, `ssa_accent_pitch`). The Braille Lite takes it as its pitch setting (32 steps of its own).

## Files

| File | What it does |
|---|---|
| `build.gradle.kts`, `settings.gradle.kts`, `gradle.properties` | The Gradle project (AGP 8.7, Kotlin 2.0), as outspoken's |
| `gradlew`, `gradlew.bat`, `gradle/wrapper/` | The Gradle wrapper (8.13) |
| `.gitignore` | Build output, `jniLibs/`, `local.properties`, `signing.properties` and keystores stay out of git |
| `app/build.gradle.kts` | The app: id `com.ssi263speech.tts`, minSdk 26, three ABIs; takes `build/android/assets` as its assets and refuses to build without the libraries and the staged files; release signing; JUnit for `src/test`, and the tests' control property |
| `app/src/main/AndroidManifest.xml` | The TTS service (direct-boot aware), the settings screen (also the framework's INSTALL_TTS_DATA), the two activities the TTS framework asks, and the file picker it may ask |
| `app/src/main/res/values/strings.xml` | The app's and the engine's names |
| `app/src/main/res/xml/tts_engine.xml` | Tells the framework which screen holds the engine's settings |
| `app/src/main/cpp/ssa_map.h`, `ssa_map.c` | An Android request's rate and pitch onto the voice's scales, the way `sd_ssi263.c` maps SSIP; the test's control switch |
| `app/src/main/cpp/ssa_engine.h`, `ssa_engine.c` | The front end in plain C over `src/csrc/voices.h` (the voices' engines and defaults, units made with `ssv_create_from`): the app's voices and where their files are (the imported ones by path, the Aicom ROMs and driver in memory; the Accent SA a unit per utterance), the settings and the request's rate and pitch, the boot settings, start, pull in chunks, stop (any thread) and cancel; `ssa_accent_pitch`, `ssa_speakout_pitch`; `ssa_probe`, the import's last check; the tests' controls |
| `app/src/main/cpp/ssa_import.h`, `ssa_import.c` | The import's judgement by content: the Braille Lite's (`bl_firmware.h`), then the Speak-Out's `SPEAKOUT.HEX` (Intel HEX, the known sha256); the test's control |
| `app/src/main/cpp/ssa_jni.c` | The thin JNI bridge to `ssa_engine` (the Aicom ROMs and driver handed over), and to the import's native part (`ssa_import.h`, `bl_state.h`) |
| `app/src/main/kotlin/com/ssi263speech/tts/SsiNative.kt` | The JNI declarations |
| `.../SsiData.kt` | The units' files: the Accents' ROMs and driver from the APK's assets; the Braille Lite's and the Speak-Out's in device-protected storage -- which voices are imported and as what, the import's move into place, removal; a developer build's bundled firmware |
| `.../FirmwareImport.kt` | What a source holds (no Android in it, so the JVM tests run it): zip layouts, the add-on, update programs, the Speak-Out's HEX, single files; the refusals in words |
| `.../SsiImport.kt` | The import on the phone: the source's bytes, the native judgement, the Braille Lite's states made and checked, each unit made to speak, the files moved into place |
| `.../SsiSettings.kt` | The settings, in device-protected storage, read once per utterance |
| `.../SsiEngine.kt` | The one engine in the process: opens it, lists the voices (the Accents built in, the Braille Lite and the Speak-Out once imported), the default voice, owns it for one utterance at a time |
| `.../SsiTtsService.kt` | The `TextToSpeechService`: voices and languages, the request's voice, rate and pitch, audio streamed block by block, stop |
| `.../SettingsActivity.kt` | The screen: Setup (the voices, the firmware's import -- the Braille Lite's and the Speak-Out's -- and removal with progress, status, a preview, the system TTS settings, licences and source) and Voice settings (every voice's own); the adb test hooks (`autospeak`, `ttstest`, `setvoice`, `setrunahead`, `import`, `removefirmware`) |
| `.../SettingsWidgets.kt` | Headings, the accessible slider, radio buttons and check boxes, from outspoken |
| `.../PreviewPlayer.kt` | The preview: rendered through the engine, played on an AudioTrack, kept as `last-render.wav` |
| `.../TtsSelfTest.kt` | The service through Android's own client, bound by package name (the default engine untouched): a file render, or a stop |
| `.../CheckVoiceDataActivity.kt` | Answers the framework's voice-data check: English always (the Accent SA), Spanish once its firmware is imported |
| `app/src/test/kotlin/.../FirmwareImportTest.kt` | The JVM tests of `FirmwareImport`, with a fake native side |
| `.../GetSampleTextActivity.kt` | The sample sentence the system's TTS settings speak |
| `licenses/DISTRIBUTION.txt` | The distribution notice the licences dialog shows first (MIT, MAME's cores, the source, Aicom's ROMs and driver, no Braille Lite or Speak-Out firmware) |
| `licenses/Kotlin-LICENSE.txt`, `Kotlin-NOTICE.txt` | The Kotlin runtime's licence (the project's and Casso's MIT licences and MAME's notices are added by `build_android.sh`) |
| `test/test_android_native.c` | The host-side test program: the app's C on the same chip, boards, hosts and voices, each case's PCM hashed; `--texts` (the Accent SA's text), `--level` (the volume test's measure), `--so-direct` and `--am-direct` (so_voice and am_voice driven directly, the references), `--import` (the import's judgement) |
| `test/test_android_native.py` | Builds and runs it on the desktop (and over adb), and compares with `bl_voice` driven as `sd_ssi263` drives it (lockstep and run ahead), with the NVDA Accent driver, and with so_voice and am_voice driven directly; the capitals; the Speak-Out import; the controls |
| `test/accent_reference.py` | The Accent SA's reference: the NVDA Accent add-on's driver under the stand-in NVDA, a fresh unit per case |
| `test/test_volume_headroom.py` | The default engine volume keeps headroom, for every voice, with controls |
| `test/test_import_native.py` | The import's native part: the list of releases, the refusals, and the states made on the device, against the listed ones, with their progress |
| `test/check_apk_no_firmware.py` | An APK carries no firmware but Aicom's three ROMs and the Accent-mini's driver (by sha256): no Blazie or Speak-Out firmware or state, by content and by name; its controls |
| `test/test_device_service.py` | The installed app's TTS service on the phone, every voice and their capitals, the Braille Lite with run ahead, its files' PCM against the same references |
