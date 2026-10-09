# Blazie firmware on BT Speak and BT Braille

This frontend runs the existing emulator's Braille Lite 2000, Braille 'n Speak 2000, and Type 'n Speak firmware
using the shared emulator core and SSI-263 voice.
The device's keyboard server supplies the original dot keys, before BRLTTY translates them. Host menus use
the installed `BTSpeak.dialogs` library. The firmware handles editing, commands, speech settings, and files.

Speech, keyboard access, and the firmware's own braille output are supported. The English Braille Lite 2000's
18 cells appear at the left of a BT Braille 20 or 40, with remaining cells blank. Routing keys are unused.

## Build and run

Run `blazie_emu`, the same command as on any other Linux machine; on a BT Speak or BT Braille it uses the device
automatically. From the repository root, with the English firmware and factory state in `firmware/blazie/`:

```sh
./build_linux.sh
./build/linux/blazie_emu
```

On the device, `blazie_emu` says "BT Speak or BT Braille detected: using its keyboard, speech and braille display."
and hands over to `blazie_emu_bt` beside it (`src/apps/blazie/bt_handover.c`), passing on `--unit`, `--firmware`,
`--config` (as `--state-dir`) and `--rate`; its terminal-only options have no equivalent here and are left behind.
The device is recognised when the system's `python3` imports the device's `BTSpeak` library and its keyboard service
answers (`kb_client.server_available()`), which is what this frontend needs to run; an ordinary PC or Raspberry Pi
has no `BTSpeak` library, so `blazie_emu` stays in the terminal there. `blazie_emu --bt-probe` says what it finds.
`blazie_emu --no-bt`, or `bt = off` under `[input]` in its `blazie_emu.ini`, keeps the terminal emulator on the
device. `bt = native` keeps `blazie_emu` itself, using the device's keyboard and braille display without this
frontend or Python ([README-btspeak.md](README-btspeak.md)); it does so too when `blazie_emu_bt` is missing. If `blazie_emu_bt` or `blazie_bt` is missing beside it, `blazie_emu` says so in one line and runs in the
terminal. `./build/linux/blazie_emu_bt` can also be run directly, with the options below. The desktop app,
`blazie_emu_gtk`, never hands over: the BT devices have no desktop.

The build produces two runtime files:

- `build/linux/blazie_emu_bt`: the executable Python frontend, bundled as a zipapp by `tools/build_bt_frontend.py`.
- `build/linux/blazie_bt`: its native worker, sharing `emu_unit.c`, the MAME Z180 core, and the existing Linux audio backend.

Keep both files. The launcher starts the worker; it does not compile it. The frontend requires Python 3.11+
and the device's installed `BTSpeak` Python package and keyboard service. It uses `dialogs.activity`, which replaced `runActivity` in the runtime inspected here.

Run from a Blazie Mode terminal on the device. Nothing is installed into or changed under `/BTSpeak` or
`/BTBraille`; no menu registration, service restart, or driver configuration is needed.

The original `./src/platforms/btspeak/blazie` convenience launcher still works for source development,
using the same compiled worker in `build/linux`. Rebuild the normal launcher after Python edits with
`python3 tools/build_bt_frontend.py`; rerun `./build_linux.sh` after native-code changes.

The existing `tools/package_linux.sh firmware/blazie VERSION` includes both runtime files under `bin/`,
this guide as `README-blazie-bt.md`, and the packaged firmware under `share/ssi263-speech/`. The Braille 'n Speak
2000's firmware is never in that package (it ships with the emulator's own downloads only); its profiles appear
when you supply it. From an unpacked package run `./bin/blazie_emu`; after installation with the existing Linux
installer, run `blazie_emu`.
That installer also installs/configures speech-dispatcher and requires its existing system configuration;
it is not needed to run the unpacked BT application. No BT device library is bundled or replaced.
Firmware is located relative to the executable, so the build and unpacked package can run from any working
directory. `--firmware` and `--backend` override those defaults. Neither runtime file needs the source tree
when distributed with the firmware folder. Objects and test programs elsewhere in `build/linux` are development
artifacts, not runtime dependencies. Saved user memory stays in the BT user directory, outside `build/`.

Optional arguments:

```sh
./build/linux/blazie_emu_bt --rate 44100
./build/linux/blazie_emu_bt --firmware /path/to/firmware --state-dir /path/to/emulator-memory
./build/linux/blazie_emu_bt --device default
```

The initial default is 22050 Hz with the automatic audio buffer (starting at 60 ms). Audio choices are remembered;
`--rate` overrides and saves the sample rate for this and later runs. Sound-device
failure is reported; it does not silently continue without a voice. Braille Lite and Braille 'n Speak require a factory state on the
first start; Type 'n Speak uses its own cold setup. An existing saved state takes precedence; a damaged state is reported instead of reset.

## Keyboard

| Physical keys | Effect |
| --- | --- |
| Dots 1–6, Space, and their chords | Sent unchanged to the original firmware |
| E-chord | Firmware Enter |
| B-chord | Firmware Backspace |
| M-chord with Dot 7: Space plus dots 1, 3, 4, and 7 | Open the host menu after all keys are released |
| Z-chord with Dot 7: Space plus dots 1, 3, 5, 6, and 7 | Save, close the emulator and terminal, and return to the host editor |
| Other combinations containing Dot 7 or Dot 8 | Ignored as a whole; never converted to a firmware command |
| L3 or R3 on BT Braille | Original Braille Lite forward bar |
| L2 or R2 on BT Braille | Original Braille Lite back bar |
| Other panel keys and all routing keys | Unassigned |

The host menu offers Resume, Save memory, Quick key response, Audio settings, Firmware, Keyboard help, and Save and exit.
The normal BT dialog controls apply while in the host menu. Menus and submenus share one terminal screen,
and returning from a submenu keeps the previously selected row. Audio settings opens on Sound buffer.
Leaving the host menu with Z-chord (Escape) or Resume announces "Menu closed" before the firmware resumes.
Save memory announces "Memory saved" and returns to the menu without requiring Enter.

The keyboard introduction is shown once per saved-memory directory, with the acknowledgement stored in
`preferences.json`. Existing preferences without that flag show it once after updating. Keyboard help
remains available from the host menu.

Z-chord with Dot 7 uses the platform's existing `Tools/deep-escape`, after the emulator has saved and restored
its host context. The helper runs detached from the terminal it closes, using noninteractive sudo when needed,
just as the normal BRLTTY binding runs it as root. If the final save fails, this handoff is not performed.
The host menu's Save and exit still returns to the shell, so both ways of leaving remain available.

## Quick key response

Open the host menu with M-chord with Dot 7 and select **Quick key response: off** to turn it on; select it again
to turn it off. The choice is kept in `preferences.json` beside the unit's saved memory and restored at launch.
The default is off, preserving original timing.

This uses the existing `emu_set_quick` option from the desktop and terminal emulators. After a key, it briefly
runs the firmware CPU at eight times its usual speed until the first spoken phoneme or a bounded timeout.
Speech synthesis retains its normal timing. Quick response ends during firmware restart so held-key reset
gestures still work. The separate experimental `run_ahead` machinery is for speech rendering and is not enabled
by this menu item. The audio buffer still contributes to the delay heard at the speaker.

Six-dot keyboard chords accumulate until the last key is released. Keys physically held are also passed to the firmware,
so restart gestures such as holding I-chord after P-chord, L work. Automatic key-repeat does not produce
extra chords. There is no translated text input, capital-letter shortcut, or substitute for Enter or Backspace.

## Audio settings

Open **M-chord with Dot 7 → Audio settings**. Choices are kept in `preferences.json` and apply across
firmware switches and launches. Saved audio choices are preserved; preferences without a buffer setting use Automatic.
Buffer labels include their timing: Automatic starts at 60 ms and increases if needed, Medium is 100 ms, and Long is 250 ms.

| Control | Choices | Initial default |
| --- | --- | --- |
| Sound buffer | Automatic (starts at 60 ms and grows up to 250 ms if playback runs dry), Medium (100 ms), Long (250 ms) | Automatic |
| Sample rate | 11025, 16000, 22050, 32000, 44100, 48000 Hz | 22050 Hz |
| Idle sound | Silent, hiss, whine, original unit behavior | Original unit behavior |
| Keep channel open | During speech only, until firmware switches off, always | Until firmware switches off |
| Startup pop and shutdown click | On/off | On |
| 10 Hz channel tick | On/off | On |

The sample rate and buffer apply to every model. Idle sound, channel behavior, pops, and ticks are hidden for
Type 'n Speak because the shared emulator does not model those effects for it. Original idle behavior means
hiss at even firmware volume settings and whine at odd settings. Firmware speech speed, pitch, inflection,
and volume still use the firmware's own commands. Quick key response remains a separate host-menu setting.

A smaller buffer reduces additional speech latency; choose a larger one if playback breaks up. Automatic
uses the existing shared audio pacing code, starting fresh when the output device reopens. The unsupported
40 ms "short" test mode is not offered. Actual latency also depends on the sound device and system mixer.

Buffer and sound-effect choices do not restart the unit. Changing sample rate saves memory and recreates
the unit from that saved memory, as the original emulator does. The worker checks audio-device support first
without playing samples and retains the old unit if saving or creating the replacement fails. A failed
preference write restores the previous audio settings. Use **Back** or cancel to return to the host menu.

## Firmware selection

Open M-chord with Dot 7, then **Firmware**. The menu lists installed, supported firmware with a factory
state or an existing saved memory. A failed switch leaves the previous unit available; a successful switch
saves it before running the new unit. The choice is remembered, or override it with `--unit bl-en` etc.
The switch announcement names the selected model and language.
The same nested and flat firmware-folder layouts as the original terminal emulator are accepted.

| Unit ID | Firmware | Saved memory | Braille |
| --- | --- | --- | --- |
| `bl-en` | `BL2ENG.BNS` | `english.state` | 18 cells |
| `bl-es` | `spanish/BL2SPA.BNS` | `spanish.state` | 18 cells |
| `tns-en` | `tns/TNSENG.TNS` | `tns_english.state` | None |
| `tns-es` | `tns/TNSSPA.TNS` | `tns_spanish.state` | None |
| `bns-en` | `bns2000/BS03ENG.BNS` | `bns_english.state` | None |
| `bns-sk` | `bns2000/BS2SLL.BNS` | `bns_slovak.state` | None |

Braille Lite and Braille 'n Speak use the factory filenames from the original emulator (`main_linux.c` KINDS).
Type 'n Speak starts cold if there is no saved memory. English and Spanish Braille Lite and Type 'n Speak
are present on this checkout; Braille 'n Speak appears when its firmware and factory state are supplied.
The firmware controls display length: speech-only models clear the cells. The older Braille Lite 18 and
40 ROMs are rejected by the existing core; a 40-cell display decoder alone does not make those ROMs runnable.

Type 'n Speak has a QWERTY keyboard. Only for that model, six-dot computer braille is converted to QWERTY
keystrokes using the original emulator's `keys.c` and `tns_term.c`. E-chord sends Enter, B-chord Backspace,
dots 3-6 chord Escape, I-chord Tab, dots 1/4 chords Up/Down, dots 2/5 chords Left/Right. Other space chords
send Control plus the character. **Send Type 'n Speak key** in the host menu accepts names such as `ctrl-e`,
`shift-a`, `alt-x`, `f1`, and `ctrl-alt-delete`, covering combinations without a direct braille shortcut.
The panel bars are unused for this model. Host menu and deep-escape chords remain available.
This adapter never changes Braille Lite or Braille 'n Speak keyboard input.

On a new Type 'n Speak, answer each setup question with `y` (English) or `s` (Spanish), seven answers in all:
initialize files, confirm, initialize flash, confirm, initialize folders, delete file area, confirm.
Wait for the next prompt: flash initialization takes about 45 seconds of clicks; the final clear takes
about 35 seconds of silence. This is the original firmware's setup of its own fresh memory.

## Memory and cleanup

Memory is kept under `script.getUserSubdirectory("blazie-emulator")`, separate from the desktop
and terminal emulator's settings. Each firmware profile has its own state file; the original English
Braille Lite memory remains `english.state`. A lock prevents two instances from writing the same unit. Memory is saved
every minute while running, on an explicit save, and on exit, SIGINT, SIGTERM, SIGHUP, or loss of the frontend.
Saves use a flushed temporary file followed by replacement of the previous state, and the folder is flushed after
the replacement so it survives a power loss. As with other processes,
SIGKILL or loss of power can lose changes since the most recent save.

The worker pauses and closes audio for a host menu. Keyboard capture is released before any host dialog or
activity indicator runs. On exit the frontend restores the previous self-voice state. Killing or disconnecting
the frontend releases its keyboard socket; the worker sees pipe EOF and saves. A worker stuck in an audio
driver is terminated with a bounded timeout; this exceptional case cannot guarantee a final save.

The native worker starts paused and uses a small private stdin/stdout protocol. Commands are `RESUME`,
`PAUSE`, `KEY held chord` (decimal firmware bitmasks), `BARS down` (1 forward, 2 back, 3 both, 0 released),
`BRAILLE`, `TNS key-name`, `QUICK 0` / `QUICK 1`, `AUDIO`,
`AUDIO rate buffer idle keep-open pop-click tick`, `SAVE`, and `QUIT`.
Responses are `READY`, `RUNNING`, `PAUSED`, `OK` (for `QUICK`, `TNS`, and audio changes), `SAVED`, `BYE`, or `ERROR reason`.
`KEY` and `BARS` have no success response. A zero chord updates only held keys. Their "Keyboard queue full" and
"Braille bar queue full" errors (keys typed faster than the unit reads them) are logged and the session continues;
every other `ERROR` ends it.
`AUDIO` queries the current options; the six-argument form changes them only while paused. Buffer is
`auto`, `medium`, or `long`; idle is 0–3; keep-open is 0–2; pop-click and tick are 0/1. A sample-rate change
can also emit `SAVED` before `OK`. The initial CLI rate accepts the same six rates as the menu.
The worker performs all unit operations on one thread, keeping its rendering independent of Python's UI.
The keyboard loop acknowledges server events before writing to the worker, using a nonblocking pipe.

## Braille display

The emulator decodes the physical display bus, rather than reading firmware RAM or translating its speech.
This preserves the firmware's own braille translation, cursor dots, and display messages. L3/R3 press the
forward contact, and L2/R2 press the back contact. The firmware still controls an 18-cell line on a BT Braille 40; extra physical cells
do not change the firmware's navigation or wrapping. BT Speak continues to work with speech alone.

The frontend polls `BRAILLE` at up to 20 Hz, with only one outstanding request. The reply is `BRAILLE ` plus
hexadecimal cell bytes (empty before the first complete latch). A complete frame is cached even when it arrives
alongside a menu command reply. Only changed frames are sent through `brl.write_dots`, padded to exactly the
device width; resuming after a host menu forces a redraw. Cursor flashing comes from the firmware's updates.

The display bus uses PPI port C bits 0, 1, and 2 for data, clock, and latch. Each byte is shifted
least-significant bit first; the clock and latch use rising edges. Cells arrive right-to-left.
The 18-cell board sends six padding bytes as three pairs. Wire bits 0 through 7 represent dots
7, 3, 2, 1, 8, 6, 5, and 4. Cursor indicators are already part of those bytes.

The bars are separate from the chord keyboard: active-low PPI port B (81h) bits 6 (forward) and
7 (back). The firmware handles messages, scrolling, direction reversal, and presses of both bars.
The native worker preserves quick taps with a 120 ms minimum contact/release interval in emulated time;
holding a button retains its contact. Both physical buttons for a direction share one contact,
so releasing one does not release the other. Pausing releases contacts and discards queued bar input.

`bl_display.h` publishes only complete, latched frames, discards partial or overlong transfers,
and ignores clock-controller activity on other PPI bits. It also decodes the 40-cell bus layout;
the supported English firmware uses the 18-cell layout, verified against its actual port traffic.

## Verification

```sh
python3 -m unittest discover -s src/platforms/btspeak -p 'test_*.py' -v
./build/linux/test_display
```

Tests cover every original dot/space/advance combination, menu press/release orders, suppression of extra dots,
keyboard acknowledgement and release, host-menu lifecycle, native save/reload, concurrent-instance exclusion,
bad states, save and audio failures, shutdown, remembered quick response, and a real firmware reset with I-chord held
while quick response is enabled. Deep-escape tests mock the platform helper and check that saving and keyboard/context
cleanup finish before it is invoked; they do not close the actual device terminal. Native tests use
temporary state directories and silent pacing; they do not capture the device keyboard. They require a built
worker and the English firmware. Actual speaker quality, physical typing, and BT Speak hardware still need
hands-on verification. Display tests cover bus edges, bit order, padding, complete-frame latching, reset,
malformed transfers, raw output sizing, partial pipe reads, menu restoration, and the real firmware's file list
and help text. The physical cells still need a user's tactile check.

Navigation tests press and release the real emulated port contacts, verify forward/back help text,
and check overlapping left/right holds. Profile tests cover all six choices, separate memories,
remembered selection, successful switching, and recovery from a failed switch. Type 'n Speak tests
exercise both installed languages, keyboard input, absent braille output, and saving.

Audio tests cover old-preference migration, every menu control, all six native sample rates, all buffer
modes, memory preservation, cancellation, device/save failures, and rollback after a preference-write failure.
Hardware listening is still needed to choose the shortest buffer that plays smoothly on a particular device.

`blazie_emu`'s hand-over is tested by `src/apps/blazie/test_bt_handover.py` (in `tools/linux_tests.sh`): the
detection against a stand-in `BTSpeak` library and keyboard service, either one missing, and the test machine
itself; the options passed to a stand-in `blazie_emu_bt`; `--no-bt`, `bt = off`, no device and a missing frontend
keeping the terminal emulator. Its control (`BLAZIE_BT_BREAK=1`) must fail. On a BT device itself, run the gate
with `BLAZIE_TEST_ON_BT_DEVICE=1`, so the test machine's own detection is expected to say yes.

## Files

| File | What it does |
| --- | --- |
| `frontend.py` | The frontend: arguments, start-up checks, the capture loop (keyboard service events to the worker, braille polling), the host menu, firmware switching, deep escape, and the final save. |
| `worker.py` | The native worker's pipe protocol: start, bounded requests and replies, `BRAILLE` frames, logged queue-full warnings, close and abort. No BT UI imports. |
| `backend.c` | The native worker (`build/linux/blazie_bt`): one unit from `src/apps/blazie/emu_unit.c` with the Linux audio code, its commands on stdin, saves to a flushed temporary file renamed over the state. |
| `keymap.py` | Six-dot chords from the device's key events: held keys, the menu and deep-escape chords, dots 7 and 8 ignored. |
| `tns_keyboard.py` | Six-dot computer braille to the Type 'n Speak's key names (Type 'n Speak only). |
| `display.py` | Firmware braille frames to the device's cells, padded to its width, sent only when changed. |
| `profiles.py` | The six firmware profiles: firmware and factory-state names in both folder layouts, each one's saved memory. |
| `preferences.py` | `preferences.json` beside the saved memory: the unit, quick key response, the audio settings. |
| `audio_options.py`, `audio_menu.py` | The audio settings and their host menu. |
| `runtime_paths.py` | The worker and firmware beside the launcher: the source tree, `build/linux`, or the package's `bin/`. |
| `blazie` | The source-tree launcher (`./src/platforms/btspeak/blazie`). |
| `test_*.py` | The unit tests (Verification above); `test_worker.py` also drives the real worker and firmware, and `test_launch.py` builds and runs the bundle. |

The bundle is made by `tools/build_bt_frontend.py`; `blazie_emu`'s hand-over to it is `src/apps/blazie/bt_handover.c`.

## Contributing

This frontend lives under `src/platforms/btspeak`, while shared firmware peripherals stay under
`src/csrc/blazie` and the shared emulator interfaces under `src/apps/blazie`. The existing Linux build,
release packager, installer/uninstaller, and test runner include the BT variant alongside the other shells.
`tools/linux_tests.sh DATA` supplies its firmware folder to the BT native tests through
`BLAZIE_TEST_FIRMWARE`; keyboard/menu/packaging tests also work without a BT runtime.

Commit source, tests, build/packaging changes, and documentation. Do not commit `build/`, firmware images,
or saved states; these are ignored.
