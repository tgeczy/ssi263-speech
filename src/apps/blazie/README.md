# The Blazie emulator

A Braille Lite 2000, a Braille 'n Speak 2000 or a Type 'n Speak you can use from a PC keyboard: Blazie's own firmware
on the emulated board
(`../../csrc/blazie`: `bl_board.c`, `tns_board.c`), with the emulated SSI-263 (`../../csrc/ssi263.c`) as its
voice. Unlike the screen-reader
drivers, the unit is not put into speech-box mode: it boots to its own main menu and behaves as the unit does,
including the channel left open (hiss or whine) until the firmware clicks it off.

| File | What it does |
| --- | --- |
| `chords.h`, `chords.c` | A braille chord from separate key presses: sent when the last key of the chord comes up. Portable. |
| `emu_unit.h`, `emu_unit.c` | One unit running in real time: create from firmware + state, render 16-bit PCM, take chords. Portable. |
| `main_win.c` | The Windows shell: the window, the menu (firmware, idle channel, keep open, pop and tick, sample rate, sound buffer, serial port, help), the keyboard, waveOut. |
| `audio_pace.h`, `.c` | The sound queue: how many blocks the sound thread keeps at the card (automatic, short, medium, long), a gap told from the card's played position, the minute's save after rendering ahead; for the Linux shells, a card's queue and played position from its delay, and the sleep until a block is wanted. Portable: `main_win.c`, `audio_linux.c`. |
| `test_audio.c` | The sound queue against a simulated sound card (calibrated on this desktop's waveOut): a steady card, a busy machine, a remote card, a slow save, and the gap detector -- for `main_win.c`'s sound thread and the Linux shells' (`audio_linux.c`); the Linux card's arithmetic; `--old` (the 0.7.0 draft's queue) is the must-fail control of run_tests and `tools/linux_tests.sh`. |
| `serial_win.c`, `.h` | The unit's serial port on a Windows COM port: the port list, and a thread moving bytes and setting the port as the firmware programs it. The portable half is `../../csrc/blazie/bl_serial.c`. |
| `tns_keymap_win.c`, `.h` | A Windows key to the Type 'n Speak's key code (measured on the running firmware). |
| `build_app.py` | Builds `blazie_emu.exe` and the test programs into `nvda/dist/blazie-emu/` (w64devkit, x64, static), the boards on MAME's Z180 (`../../csrc/cpu/z180_mame.cpp`), with the licence files beside them. |
| `test_chords.c` | The chord logic. |
| `test_emu_unit.c` | The unit headless: the boot greeting is heard, a chord is answered (a no-chord run is the control), faster than real time, its key latency, a save under 50 ms. |
| `test_clock.c` | The clock controller alone, then the English Braille Lite and Type 'n Speak setting and reading the time and date with their own commands, the clock going on and kept over a switch-off; and i-chord held through p-chord l's restart. `TEST_CLOCK_BREAK` / `TEST_CLOCK_HOLD_BREAK` put one bug back for run_tests' must-fail controls. |
| `test_flash.c` | The file flash: the Type 'n Speak's ID check passes and its flash is initialised, the erase takes ~46 s (32 s and the 14.4 s preprogramming) with the firmware's clicks, the flash kept across a save and restart; the Braille Lite's reset erases the same way, and a file moved to flash lands in the 2 MB and survives a restart. `test_flash_break.exe` (the old flash put back), `test_flash_old.exe` (the erase without its preprogramming) and `--break=instant|persist` must fail. |
| `test_idle.c` | The idle channel against Tomi's unit (the noise's level at volumes 1, 6 and 15, keep open off/until/always, the pop, the click-off, the tick); `--break=...` puts one bug back for run_tests' must-fail controls. |
| `test_serial.c` | The serial port plugged in, headless: the storage handshake answered from the far end, on every unit (below); built with the receive path cut, it must fail. |
| `test_serial_win.c` | `serial_win.c` end to end, a named pipe standing in for the COM port and this program for WinDisk; built with the receive path cut, it must fail. |
| `tns_keys.h` | The Type 'n Speak's key codes by key name: the one table `tns_keymap_win.c` and `tns_term.c` both read. Portable. |
| `tns_setup.h`, `.c` | The Type 'n Speak's first start: its cold reset's seven questions (measured), what the shells tell the person, and a headless factory setup (the questions answered in the unit's own time) for the tests and the rescue. Portable. |
| `tns_rescue.h`, `.c` | A saved Type 'n Speak that was never set up (the previews' first start missed the cold reset), told apart and set up anew with its RAM files; the old state kept beside it. Portable. |
| `test_rescue.c` | A unit made as the previews made it, told apart, rescued (its first file without the character it never stored, the flash file it lost named), and then moving a file to flash with its own command; `TEST_RESCUE_BREAK=1` must fail. |
| `blazie_files.c` | The command line for a saved unit's files (Windows, Linux, the BTSpeak): list, export to a disk image, import from one, extract to a folder, pack and unpack an image. The portable half is `../../csrc/blazie/bl_files*.c` and `fat_img.c`. |
| `make_state.c` | A unit's factory state from its firmware alone (`../../csrc/blazie/bl_state.c`'s recipe): the Braille 'n Speak 2000's, kept in `firmware/blazie/bns2000/`. |
| `test_bns.c` | The Braille 'n Speak 2000 on the Braille Lite's board: the units told apart (the voice's import still refuses it), and its words read from the chip's register writes -- Slovak at power-on and for the time -- against what the unit said; asking the English unit for the Slovak words is run_tests' must-fail control. |
| `test_files.c` | Files in and out against the units' own commands, on all four units (and the Braille 'n Speak 2000's two): the firmware's files exported exactly (the open one too), an image imported and then listed, typed into and moved by the unit, export-import-export the same image; the Type 'n Speak from its factory start (its cold reset's questions answered), its first file moved to flash whole; `--break=1..5` put one bug back each, `--break=cold` the old cold start, for run_tests' must-fail controls. `nvda/tools/files_7zip.py` checks the images in 7-Zip. |

The Linux shells (`README-linux.md`: build, keys, sound, the BTSpeak, the desktop app with Orca):

| File | What it does |
| --- | --- |
| `main_linux.c` | The terminal shell: the menu (F11), the settings and memory in `~/.config/ssi263-speech/blazie-emu`, the sound thread, headless runs for the tests. |
| `keys.h`, `keys.c` | A key as the shells see it (a character or a named key, pressed, down or up) and the key names of the settings file. Portable. |
| `term_keys.h`, `.c` | A terminal's bytes as keys: xterm, VTE and the Linux console's sequences, Alt as ESC. Portable. |
| `bl_keys.h`, `.c` | The Braille Lite's chords from keys: keys mode (by time), letters mode (computer braille, the BTSpeak), an input device's keys down and up, the hold key. Portable. |
| `tns_term.h`, `.c` | Keys as the Type 'n Speak's key events, a terminal's whole strokes with their modifiers. Portable. |
| `ini.h`, `ini.c` | The settings file, read and written back with the person's own lines kept. Portable. |
| `audio_linux.h`, `.c` | The sound card: ALSA (or PulseAudio's simple API), blocks of 10 ms, the Windows app's sound buffer (`audio_pace.c`: menu 17, `[sound] buffer=`) kept in a buffer opened for the longest queue, the card's queue and played position read on each block; `BLAZIE_EMU_AUDIO_STALL` and `_LOG` as on Windows. |
| `evdev_linux.h`, `.c` | The keyboard from `/dev/input`: keys down and up, grabbed while the program runs. |
| `serial_linux.h`, `.c` | The unit's serial port on a tty or a pseudo-terminal. |
| `bt_handover.h`, `.c` | `blazie_emu` on a BT Speak or BT Braille: the device found (the system's `python3` imports its `BTSpeak` library and its keyboard service answers), then a hand-over to the BT frontend beside it (`blazie_emu_bt`, `../../platforms/btspeak`) with the options it shares; `--no-bt` or `[input] bt = off` keeps the terminal. The desktop app never hands over: the BT devices have no desktop. |
| `test_keys.c` | The keyboard without a unit; `BLAZIE_KEYS_BREAK=1` must fail. |
| `test_display.c` | The braille display's bus (`../../csrc/blazie/bl_display.h`), without a unit: the dot wiring and bit order, clock and latch edges, the 18-cell board's padding, an overlong transfer refused, the 40-cell layout, reset. |
| `test_emu_linux.py` | The whole program headless, and its serial port end to end in a pseudo-terminal. |
| `test_bt_handover.py` | The BT hand-over: the detection against a stand-in `BTSpeak` library (and this machine, which must say no), the options a stand-in frontend receives, `--no-bt`, `bt = off`, no device and a missing frontend keeping the terminal; `BLAZIE_BT_BREAK=1` must fail. |
| `main_gtk.c` | The desktop shell, for Orca: one GTK 3 window -- the Windows app's menu bar, a keyboard area that takes keys down and up (chords and keys held through `bl_keys.c`, the Type 'n Speak through `tns_keys.h`) with an accessible name, a status line announced to the screen reader, GTK dialogs and file chooser. Sound, settings and memory as `main_linux.c` (its unit table and settings text copied, as `main_win.c` keeps its own; `test_emu_gtk.py` checks the settings text matches). |
| `test_emu_gtk.py` | The desktop app in Xvfb: keys through the X server, the window read through AT-SPI as Orca reads it; `BLAZIE_GTK_BREAK=noname` and `BLAZIE_KEYS_BREAK=1` must fail. |

## Keys

**Braille Lite**, while the window is in front: F D S = dots 1 2 3, J K L = dots 4 5 6, the space bar = space, A or ; = the advance
bar. There are no cursor-routing keys and no dots 7 and 8: the Braille Lite 2000 has none. Every other key goes to
Windows (Alt opens the menu). The keys can be changed in `blazie_emu.ini` beside the program, section `[keys]`
(`dot1=F`, ..., `space=space`, `advance=A ;`).

**Braille 'n Speak 2000**: the Braille Lite's keys, the same six dots and space bar. It has no braille display and no
advance bar (its firmware never reads that bit).

**Type 'n Speak**: the whole keyboard is the unit's, Alt and the function keys included. **Alt+Shift+F** (or F11)
always opens this program's menu, whichever unit is running (the unit's held keys are let go first). A key goes to the unit as it goes down and again as it comes up (bit 7 = down), as the unit's own keyboard
sends them; auto-repeat is left to the unit.

The Braille Lite's key port: dot 1 = bit 0 .. dot 6 = bit 5, space = bit 6, and bit 7 for the advance bar (silent in the main
menu, as moving a display would be; still to be confirmed on the unit).

**Keys held while the Braille Lite starts.** A chord goes to the unit when its keys come up, but the keys you are
holding down are also on the unit's key port while you hold them, and the firmware looks there as it starts: i-chord
held is the cold reset ("initialize file system?", then the flash, the folders, and "delete all data in file area"),
all seven keys the warm reset, space a silent start, and so on (the Help file's list). The unit starts when it is
switched on and when p-chord, l restarts it. So: p-chord, l, then press and hold i-chord at once, until the unit asks
its first question. The firmware reads the keys about 0.45 s after l comes up (0.1 s to restart, 0.35 s into the
start): hold the chord by then. A chord the start has read is not sent again when you let go of it. Settings > Quick
key response ends at the restart, so the start keeps the unit's own pace.

**p-chord, l** (switch languages) is the Braille Lite 2000's other firmware bank: the firmware switches the program
flash's bank (port E0h bit 4), checks that a program is there, and restarts into it. The emulator holds the same
firmware in both banks, so the unit restarts in the same language, its files and settings kept.

## Settings

Settings > Sample rate (11025 to 48000 Hz; the unit restarts at the new rate with its memory kept). The unit's own
speech settings -- rate, pitch, inflection, volume -- are set on the unit, with its own keys, as on the real one.

The Braille Lite's idle channel, as Tomi's unit sounds (measured: `../../hosts/blazie_idle.py`, `tools/idle_sounds.py`;
the model: `../../csrc/blazie/bl_idle.c`). In `blazie_emu.ini`, section `[sound]`:

| Setting | Menu | Choices |
| --- | --- | --- |
| `idle` | Idle channel | `unit` (the default: the hiss at even volumes, the whine at odd, as the unit), `hiss`, `whine`, `off` |
| `keep_open` | Keep the channel open | `off` (silent as soon as speech ends: heard only under speech and 0.3 s after), `until` (the default: until the unit clicks it off, ~10 s after speech), `always` (never stops) |
| `pop_click` | The pop ... the click ... | `1` (the default): the pop when a line opens the channel after the click-off, ~0.26 s before its first phoneme, and the click at the click-off; heard with `until` only |
| `tick` | The 10 Hz tick | `1` (the default): the firmware timer's faint tick, every 100 ms while the channel is heard |

The idle noise has the same level at every unit volume, as on the unit: at volume 1 it is 29 dB under the speech, at
volume 15 50 dB under. The pop and the click are as loud at every volume too, and much louder than the speech at low
volumes (the recordings: +0.87 and -0.53 of full scale; at volume 1 the speech's loud vowels are at -29.5 dBFS). They
are the unit's line out as the line-in recorded it; on the unit's own headphones they decay faster. The screen-reader
drivers keep their own hiss and whine (no noise floor, no pop, click or tick).

## The serial port: WinDisk, PCDISK, a terminal

Settings > Serial port plugs the unit's serial port (its RS-232 port) into a COM port of this PC. The menu lists
the COM ports Windows has at the moment you open it, each with its name from Device Manager, for example "COM10,
com0com - serial port emulator", and "None (not connected)" first; the one in use is checked. The choice is kept
in `blazie_emu.ini` (`[serial]`, `port=COM10`) and used again next time; if that port is gone, the program says
so and starts with the serial port unplugged. The window's title says which port the unit is on ("Braille Lite
2000 (English), serial port on COM10").

**WinDisk on the same PC** needs a virtual null-modem cable: a pair of COM ports wired to each other. com0com (a
free driver) makes one, for example COM10 and COM11. Keep its default wiring (each side's RTS to the other's CTS,
DTR to DSR and DCD); its "emulate baud rate" option is not needed. Choose one end in the emulator (Settings >
Serial port > COM10) and the other end in WinDisk (COM11). A real null-modem cable to another PC works the same
way with the real COM port.

Then use the unit as you would with the disk drive or WinDisk: on the Braille Lite, s-chord (Storage) and a
command letter -- d for a directory, l to load a file, s to save one -- or t-chord in the Files menu to send or
receive several files; on the Type 'n Speak, F8 or Alt+S. The unit finds the far end by itself: it switches its
serial port on, calls at 19200 bit/s, and when nothing answers says "storage device missing". For a terminal
program, turn the serial port on in the unit's Status menu (dots 3-4 chord, f, y); it runs at the unit's own
settings, 9600 bit/s, 8 data bits, no parity, software handshake unless you change them there.

What is carried, measured on the running firmware (`../../csrc/blazie/bl_serial.h`): the unit's RS-232 port, at
the rate, data bits and parity the firmware programs, changed on the COM port in step with the bytes (the storage
commands switch to 19200 and back). DTR is on while the unit's serial port is on; RTS follows the unit's own
handshake line. The unit's XON and XOFF go through as they are, and so do the far end's: Windows' own flow
control is off. The far end's CTS, DSR and DCD are not watched (the unit always sees them on). The disk drive's
own port on the Braille Lite is not carried: WinDisk and PCDISK talk over the RS-232 port.

Not yet tried: WinDisk itself against the emulator (the machine it was built on has no com0com). The tests answer
the unit's call from a program instead: the unit calls with XON ENQ at 19200 8N1, the far end answers ACK, the unit
answers 'C' and says "storage"; the directory command then goes out as ENQ, "d", carriage return.

Settings > Quick key response (off by default): after a key the firmware works before it speaks -- speech 283 ms after
a chord in the English Braille Lite's main menu, 107 ms in the Spanish one, 243 ms after a Type 'n Speak key (chip time,
test_emu_unit's "key latency"), the unit's own pace at its 6.144 MHz clock.  On, the CPU runs 8 times faster from a key
until the first spoken phoneme loads: 56, 83 and 51 ms.  The phonemes and every register value are unchanged; only the
wait before the first one is shorter than on the real unit.

Settings > Sound buffer (`[sound] buffer=` in `blazie_emu.ini`; `audio_pace.h`): how much sound is kept queued at the
sound card, in blocks of 10 ms (`[sound] block_ms`, 5-20).  A key's speech plays behind it.

| Choice | Queue | |
| --- | --- | --- |
| `auto` (the default) | 60 ms, growing (100, 150, 220, 250 ms) each time the card runs dry, for the session | a key's speech ~20 ms later than 0.7.0's on a PC that never breaks up |
| (`short`) | 40 ms | no longer offered: the 0.7.0 draft's four blocks broke up on a ROG Ally played directly, and 50 ms did too; a settings file's `short` now reads as `auto` |
| `medium` | 100 ms | |
| `long` | 250 ms | Remote Desktop, a Bluetooth headset, a handheld PC on battery |

Why (Tomi, 2026-10-01: "the emulator stutters horribly ... but not the nvda side"): measured on this desktop, the unit
renders 10 ms blocks in 0.4-0.7 ms each (14-26 times real time; the worst block 1.85 ms, through the Type 'n Speak's
flash erase too, and with every core loaded), and a save takes 0.5-1.4 ms (the Type 'n Speak's 5 MB too).  Neither
is the stutter.  waveOut is: it hands a block back only once it has played it, and the card's pipeline holds ~25 ms
of what is queued, so four blocks of 10 ms leave ~10 ms to spare -- the sound thread held up 10 ms by anything (Remote
Desktop's audio, a power-saving or busy machine) is a 10 ms gap, every time.  The thread held up a fixed time each
second (the sound lost, by the card's played position against the wall clock):

| Blocks queued | held 10 ms | 20 ms | 30 ms | 50 ms | 5% of blocks held up to 60 ms |
| --- | --- | --- | --- | --- | --- |
| 4 (0.7.0) | 110 ms lost of 12 s | 330 | 390 | 540 | 1.6-2.0 s of 15 s |
| 8 | 0 | 0 | 0 | 50 | 370 ms |
| 12 | | | | | 30-50 ms |

The app itself, held up the same way (`BLAZIE_EMU_AUDIO_STALL=60`, 30 s): short lost 3.8 s in 85 gaps; automatic
lost 58 ms in 3 gaps while it grew to 220 ms, then none; long lost 16 ms.  Unloaded, and with every core busy or the
program on one core with four busy threads, no gap at any setting (the sound thread runs at time-critical priority).

A gap is found by the card's played position against the wall clock: counting the blocks handed back cannot see it
(waveOut keeps the last one until the next comes).  `test_audio.c` runs the queue against a simulated card calibrated
on the table above; `--old`, the 0.7.0 draft's queue, is run_tests' must-fail control.  The minute's save runs on the
sound thread after it has rendered 50 ms ahead, so the card plays on while it writes and no key waits for it
(`test_emu_unit` checks a save takes under 50 ms).  `BLAZIE_EMU_AUDIO_LOG=file` logs each wake of the sound thread:
its renders, how far ahead it is, the gaps found and the saves.  The program also asks Windows not to slow it down to
save power (EcoQoS; a precaution, not seen here).

## Firmware

Never in the repository. A release puts `firmware\` beside the program (`BL2ENG.BNS` + `bl2_2003_warm.state`,
`tns\TNSENG.TNS` and `tns\TNSSPA.TNS` (the September 2000 revision; the Type 'n Speak needs no state), and
`spanish\BL2SPA.BNS` + `spanish\bl2spa_fresh.state`, the state the Spanish driver uses; the "warm" one has
speech off). Run from the source tree, the program finds
`firmware/blazie/` itself; `firmware_dir=` in `[unit]` overrides both.

The Braille 'n Speak 2000 is in the Firmware menu when `bns2000\BS03ENG.BNS` (English, June 24, 2003) or
`bns2000\BS2SLL.BNS` (Slovak) is there (Linux: menu numbers 15 and 16, `--unit bns-en`, `bns-sk`), each with its
factory state beside it, made from the firmware by `make_state.exe` (the three power-ons that made the English Braille
Lite's state, three seconds; `firmware/blazie/README.txt` has the commands and hashes):

    make_state bns2000\BS2SLL.BNS english bns2000\bs2sll_fresh.state

## The Braille 'n Speak 2000

The Braille Lite 2000 without its braille display, on the same board (`../../csrc/blazie/bl_board.h`, `bl_model`).
Run side by side through the same keys at the same instruction counts, its firmware and the Braille Lite's use the
same ports with the same values -- the keyboard, the SSI-263, the memory and flash banks, the power bits, the battery
gauge, the clock controller -- run the same memory test, keep the same file system in the same 2 MB flash, and ask
the same questions at the same points; only the display's traffic on the 8255 is missing.  The board tells the two
apart by the firmware's idle loop.  The Slovak unit speaks Slovak through the same chip (its own letter-to-sound
rules: a key's answer takes ~443 ms of chip time, the English ~247); it takes the date in another order than the
English unit's month, day, year (those digits set October 9).

## The clock

The time and date come from the units' clock controller, a separate chip the firmware calls over the Z180's clocked
serial port (`../../csrc/blazie/bl_clock.h` says what was measured). Before it was modelled, the firmware found no
clock: it said "reset clock, first date, then time", the time stood still and the year read 1999 (Jayson).

The first time a unit starts, its clock is set from this PC's clock. It then runs in the unit's own time, is kept in
the saved state, and goes on by the time the program was closed, as the unit's battery kept it. Set it with the
unit's own commands: on the Braille Lite o-chord, s, d (the date, m m d d y y) and o-chord, s, t (the time, h h m m,
then a or p); o-chord, t and o-chord, d read them. On the Type 'n Speak F9, s, d and F9, s, t; F4 and F5 read them.

**The year: 1989 to 2020 only.** The controller holds the year in 5 bits counted from 1989, and the 2003 firmware
sends a year as two digits plus 11: from 2021 on the number spills into the field that holds the hour. A real unit
does the same: typing 26 as the year sets the hour to 5 and leaves the year as it was. So the emulator starts the
clock 28 years back (2026 is 1998, 2027 is 1999), where the calendar is the same: 1 January falls on the same day
and the leap years line up, so the weekdays the unit gives are this year's, and stay right as the unit counts on
into the next years. (Two far-off limits remain: the unit's own year wraps from 2020 back to 1989, so a unit started
in 2048 or later loses the calendar at its next New Year, and 2100 is not a leap year.) To set the date yourself, use that year too. Thanks to
Jayson (issue #3). Units saved by 0.7.0 to 0.7.5, which used the nearest year with the same calendar (2026 was 2015,
and its New Year made 2027 a leap year), are moved onto the new year when they are loaded, with an alarm set for a
date, unless you set a different date yourself.

The alarm (o-chord, s, a) is the controller's as well: it goes off at the start of the minute set, with x for any
hour, day or month, while the unit is running.

## What the unit keeps

As a real unit keeps its battery-backed RAM, its file flash and its clock while switched off, the program saves them
on exit (and when you switch units) to `%APPDATA%\ssi263-speech\blazie-emu\english.state` or `spanish.state`, and starts from
them next time. The first time, and after Firmware > Back to the factory state, it starts from the shipped state; the
Type 'n Speak starts as a new unit, with its cold reset (below, "The Type 'n Speak's first start").

### The Type 'n Speak's first start

The program holds Ctrl+Alt+Del at power-on: the unit's own cold reset, which sets its defaults (without it blank RAM
leaves the volume at 0) and asks how to set itself up. Before it starts, the program shows what to answer (and the
Keys box says it again). Press y for each question (the Spanish unit: s), seven times:

1. "initialize file system?" y, "are you sure?" y -- "system initialized".
2. "initialize flash system?" y, "are you sure?" y -- "please wait", then the flash chip's ~46 s erase, with clicks.
3. "initialize folder system?" y -- "Type 'n Speak ready", the date, "help is open".
4. "delete all data in file area." y, "are you sure?" y -- about 35 s of silence while it clears its memory (keys are
   ignored), then "system initialized" and it starts again: ready.

Any other key asks the same question again. n to the flash makes it ask about the flash at its next start; n to the
last question leaves the file area as it is (a new unit's is empty). But n to the file system or to the folders
leaves a unit that cannot keep files -- its first file loses its first letter, and a file moved to flash is lost --
so the program offers to set it up again when it next starts it (below). Closed while it still asks, it is not set
up either, and is offered the same way.

**Why the questions.** The units shipped set up, but this is the first start a Type 'n Speak owner met after
the cold reset its help file describes, and testers liked hearing the unit's own setup. The 0.6 and 0.7 previews
sent the keys in a way the firmware took as its warm reset: it read one key early, and the three it then checks
(each a different one of Ctrl, Alt, Delete) ended with a key coming up. The warm reset sets up the flash but not the
file system or the folders, so a new file was put into the program's last byte (its first character lost) and a file
moved to flash went into folder 0, the deleted mark, without its text: lost (Timothy: "TNS asks to initialize";
Jayson and Timothy: "flash not working"). Ctrl now goes twice, and the unit runs its real cold reset.

**A Type 'n Speak saved by the previews.** When the program starts a saved Type 'n Speak whose file system or folders
were never set up, it says so and asks: Yes sets it up now (the unit's own setup, its questions answered for you, a
few seconds) and keeps its RAM files in its RAM startup folder -- a first character the old unit never stored stays
missing, and files lost moving to flash are only named, since their text was never written -- with its settings back
to the factory's; No starts it from the factory state (its questions); Cancel starts it as it is. Yes and No keep
the old memory beside it as `tns_english.state.before-setup` (or `tns_spanish`). `blazie_files list` also says when
a saved unit was never set up, and names the flash files it lost.

A state holds no CPU registers (the unit starts from it as from power-on), so the states saved by the builds before
0.7, on z180emu's Z180, load and run on MAME's unchanged: the same RAM, flash and clock, the same format.

## The file flash

Both units keep their files in a 29F016-style flash chip, 2 MB as the firmware manages it (`../../csrc/blazie/flash29.c`;
the Braille Lite pages it 512 KB at a time through port E0h bits 0-1, the Type 'n Speak 128 KB through F0h). The
Type 'n Speak's firmware reads the chip's ID before it offers the flash at all. An erase takes the chip's typical time
(the Am29F016 data sheet: 32 s for the whole chip, 1 s a sector, plus the embedded algorithm first programming every
byte it erases to 00h, which those times exclude: 14.4 s for the whole chip) and the firmware waits on the chip's
status, clicking through the speech chip every ~1.7 s (Braille Lite) or ~2.1 s (Type 'n Speak) meanwhile, a fixed count
of its status polls apart (each click: R4 F0, R1 F0, R2 FE, R3 58, phoneme 17h, then PA): initialising the flash -- the
Type 'n Speak's first start, the Braille Lite's reset -- is ~46 s of clicks, then "flash initialized" or
"ready". A Braille Lite state is 768 KB (256 KB RAM + the flash's first 512 KB) while the rest of the flash is erased,
2.25 MB once files reach it; both load.

## Licence

MIT (`LICENSE` beside the program), so it can be ported (to another screen-reader platform, a Linux device, ...),
with Casso's MIT notice (`licenses/Casso-MIT.txt`: the chip model draws on it). The boards run on MAME's Z180 core
(`../../csrc/cpu/z180_mame.cpp`), which keeps its BSD-3-Clause licence (`licenses/MAME-Z180-core-BSD-3-Clause.txt`).
`build_app.py` puts all three beside the program. Since 0.7 no z180emu (GPL) is in the program or its tests:
`python tools/check_no_gpl.py nvda/dist/blazie-emu` (run_tests runs it). The firmware keeps its own terms and is not
part of the program.

## Files in and out: disk images

Firmware > **Export files to disk image (.img)** writes every file the unit holds into a FAT disk image; Firmware >
**Import files from disk image (.img)** makes the unit's files what an image holds. No cable, no WinDisk. The image
opens in 7-Zip (and mounts on Linux: `mount -o loop`). How the units keep their files was measured on the running
firmware: `../../csrc/blazie/bl_files.h`.

**The image.** One folder for each of the unit's folders, named as the unit names them: `ram startup` and `flash
startup` (Spanish units: `RAM inicial`, `FLASH inicial`; the Slovak Braille 'n Speak 2000: `ram súbory`, `fleš
súbory`, its names read as code page 852 -- `blazie_files --codepage=852` on its saved state), and any you made in
folder mode. Each file is under its unit
name, with its exact bytes, its time and date, and read-only if you protected it. Text keeps the unit's line ends (a
lone carriage return); a grade 2 file (on the Braille Lite: no extension, or `.brl`; on the Type 'n Speak `.brl`,
`.brf`) holds its braille as ASCII braille, like a `.brf` file, not translated to print. The help file is not exported
(its text is the firmware's). The file the unit has open is exported as it is now, with what you typed since you
opened it.

**Import** makes the unit's files what the image holds, and says what it did:
- a file whose name, folder and bytes match is left alone (so export, import, export gives the same image);
- new bytes for a file the unit has: rewritten where it is (RAM or flash), its type and protection kept;
- a new file goes into the folder it is in in the image (a flash folder: into flash; a RAM folder: into RAM); a file
  at the top of the image goes to the flash startup folder; an image folder the unit lacks becomes a new folder
  (a flash folder); folders inside folders are skipped; an empty file goes to RAM (the unit keeps none in flash);
- a file the image lacks is deleted -- except the clipboard, the datebook and the file the unit has open;
- line ends from a PC editor (CR LF or LF) become the unit's CR in text files;
- names become names the unit takes: lower case, one dot, an extension of up to 3 letters, 20 characters (the
  unit's names are one list across its folders: two files of the same name are not both imported).

The unit must not be writing its flash (the import says so and waits for you); it is switched off, its files
changed, and switched on again, so the firmware finds them as if it had written them itself. When the image lacks
files the unit has, the emulator names them and asks before deleting them (No imports nothing). The unit as it was
before an import is kept beside its saved state (`english.state.before-import`, ...): to undo, close the emulator and
copy it over the `.state` file. The file the unit has open is rewritten too if the image changes it, its cursor put
at its top (that case is not yet tried against the firmware; the tests rewrite and delete files around the open one).

**Editing an image.** 7-Zip opens images but cannot change them, and Windows does not open them by itself. Either
use a tool that mounts disk images (OSFMount, ImDisk), or take the image apart into a folder and put it back:

    blazie_files unpack "Braille Lite 2000 (English) files.img" myfiles
    (edit, add or delete files in myfiles\ram startup, myfiles\flash startup, ...)
    blazie_files pack myfiles changed.img

then Firmware > Import files from disk image. The same tool works on a saved unit directly (close the emulator
first: it saves its unit when it closes), for example on Linux or the BTSpeak:

    blazie_files list english.state
    blazie_files export english.state english.img
    blazie_files import english.state changed.img      (--dry-run: say what would happen; it keeps the old
                                                        state as english.state.before-import)
    blazie_files extract english.state myfiles --crlf  (each file as a PC text file, CR LF)

**A Type 'n Speak that was never set up** (saved by the 0.6 or 0.7 previews, or a setup question answered n: "The
Type 'n Speak's first start" above): the import refuses ("the unit's folders were never set up"); export works. The
program offers to set it up, keeping its RAM files, when it starts it.

## Not yet

- An Android shell (the portable files are ready for it). The Linux shells are `main_linux.c` (a terminal) and
  `main_gtk.c` (a GTK window, for Orca): `README-linux.md`.
