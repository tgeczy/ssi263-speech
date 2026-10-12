# Blazie for macOS

This is a native AppKit app using the same portable emulation core as the Windows and Linux versions. It runs on macOS 12 or later and builds with Xcode's command line tools. No third party runtime is required.

From the repository root:

```sh
./build_macos.sh
open build/macos/Blazie.app
```

The build produces `build/macos/Blazie.app`. It compiles the project's SSI-263 and Z180 emulator sources into the app and ad hoc signs the local bundle. To build for an older macOS release, set `MACOSX_DEPLOYMENT_TARGET` (minimum 12.0) before running the script.

## First run

Choose **File → Open Firmware…** and select a `.BNS` or `.TNS` update file you own. Firmware is not part of this repository or app. The first start of a Braille Lite or Braille ’n Speak creates its factory memory from the firmware; this can take several minutes. Each firmware file's memory is then saved under `~/Library/Application Support/Blazie/` every minute, when you choose **Save Unit**, when switching firmware, and on quit. The last opened firmware is restored at the next launch.

If saving fails during quit, Blazie stays open and reports the error so the active unit's memory is not discarded.

A new Type ’n Speak starts with its own seven setup questions. Answer **y** for English firmware or **s** for Spanish firmware. Its flash initialization takes about 46 seconds, followed by the unit's file area initialization.

## Files in and out

Use **File → Export Files to Disk Image…** to save the unit's current files as a FAT `.img`. Each unit folder becomes a folder in the image, and the files retain their names, bytes, dates, and protection. The unit keeps running during export.

Use **File → Import Files from Disk Image…** to load a FAT image. Import makes the unit's files match the image: it adds and rewrites files, and can delete files missing from the image. Blazie shows the deletion list and asks before applying it. The unit must have finished writing flash. On a successful import, Blazie saves the previous memory as a `.before-import` file in `~/Library/Application Support/Blazie/` and restarts the unit with the imported files.

Binary BASIC programs in an image keep their original bytes. When an older import damaged a BASIC program by changing an executable byte into a line ending, Blazie checks its records, expressions, data, and jump addresses and repairs bytes whose original values are identifiable. This includes the bundled BASIC games. It repairs files when importing the image or opening a saved unit. Before changing a saved unit, it keeps a `.before-basic-repair` copy of its memory.

## Keyboard

- **Braille Lite / Braille ’n Speak:** F D S are dots 1–3; J K L are dots 4–6. Hold a chord together and release its last key to send it. Space is the space bar. A or semicolon moves the Braille Lite's advance bar. Keys held during a firmware restart are visible to the unit.
- **Type ’n Speak:** The ordinary keyboard sends its key down and up events, including Shift, Control, Option, arrows and F1–F10. macOS Command shortcuts belong to the app.

The window shows the Braille Lite's hardware display in Unicode braille. The app uses the system audio output at 44.1 kHz. The keyboard area, status, and display have accessibility labels for VoiceOver.

## Idle channel sound

**Settings → Idle Channel Sound** offers **As the Unit** (the default: hiss at even volume settings, whine at odd ones), **Hiss**, **Whine**, and **No Hiss or Whine**. **Settings → Keep Channel Open** chooses whether it is heard only during speech, until the firmware clicks it off (the default), or always. The separate **Pop When Opening and Click When Closing** and **10 Hz Tick** settings are on by default. These controls model the Braille Lite and Braille ’n Speak idle channel; they do not change the Type ’n Speak. The choices are saved for the next app launch.

## Virtual serial port

Choose **Settings → Serial Port → Virtual Port for This Mac** to connect the unit's serial port to another program on the same Mac. Blazie shows a path below the braille display; **Settings → Serial Port → Copy Port Path** copies it. Select that path as the serial device in a terminal or disk program. Blazie creates a stable link at `~/Library/Application Support/Blazie/serial` when that name is available, so another program can keep the same device setting across launches. The virtual device itself is recreated when Blazie starts.

The connection follows the unit when you switch firmware or import a disk image, and the choice is restored when you next open Blazie. The unit controls its own serial baud rate, data format, and whether its port is powered. Choose **None (Disconnected)** to unplug it. This connects the unit's main serial port, used by storage commands and terminal software; it does not expose the separate portable disk-drive port.

The macOS shell does not yet expose the Windows and Linux shells' detailed sound settings or a physical serial adapter.
