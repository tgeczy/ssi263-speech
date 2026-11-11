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
| `emu_catalog.h`, `.c` | The units a firmware folder can run, the one list every shell reads (Windows, GTK, the terminal, the BT Speak's, Apple's unit tab and Android's unit screen): the eight the emulators always had, with their saved-state names and settings values unchanged, then every Braille Lite release in `../../csrc/blazie/bl_languages.txt` whose firmware is in the folder (any .BNS, by its image) -- never one whose firmware is not; a release unit's first state made by its own recipe. Portable. |
| `test_catalog.c` | The catalog headless: the eight unchanged; in a folder holding two release files exactly those two release units, one when a file is taken away; a release unit's first state its release's hash; `CATALOG_BREAK=1` (a unit listed without its firmware) must fail. |
| `main_win.c` | The Windows shell: the window, the menu (firmware -- its units from `emu_catalog.c`, the release units under More languages --, idle channel, keep open, pop and tick, sample rate, sound buffer, serial port, help), the keyboard, waveOut. |
| `audio_pace.h`, `.c` | The sound queue: how many blocks the sound thread keeps at the card (automatic, short, medium, long), a gap told from the card's played position, the minute's save after rendering ahead; for the Linux shells, a card's queue and played position from its delay, and the sleep until a block is wanted. Portable: `main_win.c`, `audio_linux.c`. |
| `test_audio.c` | The sound queue against a simulated sound card (calibrated on this desktop's waveOut): a steady card, a busy machine, a remote card, a slow save, and the gap detector -- for `main_win.c`'s sound thread and the Linux shells' (`audio_linux.c`); the Linux card's arithmetic; `--old` (the 0.7.0 draft's queue) is the must-fail control of run_tests and `tools/linux_tests.sh`. |
| `brl_win.c`, `.h` | The unit's braille display in the Windows window (Settings > Braille display, Alt+Shift+B; off by default): a read-only field holding the cells as Unicode braille, which the screen reader shows on the user's display; the field passes every key to the window and tells the screen reader of each change (a name change event), and has no name. The bars from the keyboard: period and comma, ; and A, Alt+Shift+Right and Left (`main_win.c`). |
| `test_brl_win.c` | The field in a hidden window: keys to the window, cells as Unicode braille; `test_brl_win_break.exe` (keys kept in the field) is its must-fail control. |
| `serial_win.c`, `.h` | The unit's serial port on a Windows COM port: the port list, and a thread moving bytes and setting the port as the firmware programs it. The portable half is `../../csrc/blazie/bl_serial.c`. |
| `tns_keymap_win.c`, `.h` | A Windows key to the Type 'n Speak's key code (measured on the running firmware). |
| `build_app.py` | Builds `blazie_emu.exe` and the test programs into `nvda/dist/blazie-emu/` (w64devkit, x64, static), the boards on MAME's Z180 (`../../csrc/cpu/z180_mame.cpp`), with the licence files beside them. |
| `test_chords.c` | The chord logic. |
| `test_emu_unit.c` | The unit headless: the boot greeting is heard, a chord is answered (a no-chord run is the control), faster than real time, its key latency, a save under 50 ms; the English Braille Lite's double-speed switch with the upgrade installed and without (`TEST_EMU_DOUBLE_BREAK` the control). |
| `test_clock.c` | The clock controller alone, then the English Braille Lite and Type 'n Speak setting and reading the time and date with their own commands, the clock going on and kept over a switch-off; and i-chord held through p-chord l's restart. `TEST_CLOCK_BREAK` / `TEST_CLOCK_HOLD_BREAK` put one bug back for run_tests' must-fail controls. |
| `test_flash.c` | The file flash: the Type 'n Speak's ID check passes and its flash is initialised, the erase takes ~46 s (32 s and the 14.4 s preprogramming) with the firmware's clicks, the flash kept across a save and restart; the Braille Lite's reset erases the same way, and a file moved to flash lands in the 2 MB and survives a restart. `test_flash_break.exe` (the old flash put back), `test_flash_old.exe` (the erase without its preprogramming) and `--break=instant|persist` must fail. |
| `test_brand.c` | Blazie's name brander (BSNAME.BNS in firmware/blazie/, never in the repo; absent: skipped) on the English Braille Lite: it programs the owner's name at the program flash's 00090h and nothing else, p-chord th then says "owner is" and the name (an unbranded unit only "ok"), and the emulator keeps the name beside the state, puts it back, removes the file when the name is blanked, and takes no name from another firmware's file. `test_brand_nokeep.exe` (`EMU_NO_OWNER_KEEP`: the name not kept, as before) and `test_brand_break.exe` (the program flash's writes dropped) must fail. |
| `test_idle.c` | The idle channel against Tomi's unit (the noise's level at volumes 1, 6 and 15, keep open off/until/always, the pop, the click-off, the tick); `--break=...` puts one bug back for run_tests' must-fail controls. |
| `test_serial.c` | The serial port plugged in, headless: the storage handshake answered from the far end, on every unit (below); built with the receive path cut, it must fail. |
| `test_serial_win.c` | `serial_win.c` end to end, a named pipe standing in for the COM port and this program for WinDisk; built with the receive path cut, it must fail. |
| `tns_keys.h` | The Type 'n Speak's key codes by key name: the one table `tns_keymap_win.c` and `tns_term.c` both read. Portable. |
| `tns_setup.h`, `.c` | The Type 'n Speak's first start: its cold reset's seven questions (measured), what the shells tell the person, and a headless factory setup (the questions answered in the unit's own time) for the tests and the rescue. Portable. |
| `tns_rescue.h`, `.c` | A saved Type 'n Speak that was never set up (the previews' first start missed the cold reset), told apart and set up anew with its RAM files; the old state kept beside it. Portable. |
| `test_rescue.c` | A unit made as the previews made it, told apart, rescued (its first file without the character it never stored, the flash file it lost named), and then moving a file to flash with its own command; `TEST_RESCUE_BREAK=1` must fail. |
| `blazie_files.c` | The command line for a saved unit's files (Windows, Linux, the BTSpeak): list, export to a disk image, import from one, add files to flash startup (`add`: the menu's Load programs and files into flash), extract to a folder, pack and unpack an image. The portable half is `../../csrc/blazie/bl_files*.c` and `fat_img.c`. |
| `make_state.c` | A unit's factory state from its firmware alone (`../../csrc/blazie/bl_state.c`'s recipe): the Braille 'n Speak 2000's, kept in `firmware/blazie/bns2000/`. |
| `test_bns.c` | The Braille 'n Speak 2000 on the Braille Lite's board: the units told apart (the voice's import still refuses it), and its words read from the chip's register writes -- Slovak at power-on and for the time -- against what the unit said; asking the English unit for the Slovak words is run_tests' must-fail control. |
| `test_files.c` | Files in and out against the units' own commands, on all four units (and the Braille 'n Speak 2000's two): the firmware's files exported exactly (the open one too), an image imported and then listed, typed into and moved by the unit, export-import-export the same image; the Type 'n Speak from its factory start (its cold reset's questions answered), its first file moved to flash whole; `--break=1..5` put one bug back each, `--break=cold` the old cold start, for run_tests' must-fail controls. `nvda/tools/files_7zip.py` checks the images in 7-Zip; `nvda/tools/files_keep.py` checks `blazie_files import --keep-exact` (an LF and a CR LF file kept byte for byte; `TEST_FILES_BREAK=8` ignores the option, for its control). |
| `test_ram_full.c` | The units' 768 KB of RAM kept in their state (a unit in use lost 37 files the firmware had put past 7FFFFh, when a state kept only 256 KB): the Braille Lite and the Braille 'n Speak 2000 from their shipped states, RAM filled with made-up files, a three-page file in flash moved to RAM with the unit's own commands. With one page free below 80000h it lands across it and must be whole after the save and after switching off and on; with all 768 KB full it must stay in flash and every file stay whole. `TEST_RAM_FULL_BREAK=1` saves the first 256 KB only (bl_board.h `bl_ram_break`) for the must-fail control. |
| `test_games.c` | Blazie's games from their disks (simon.bns, hangman.bns in firmware/blazie/games/, never in the repo; absent: skipped) run from the unit's files with its own command: Simon's first tone and its time-out on the Braille Lite, the Braille 'n Speak 2000 and the Type 'n Speak (RetroBunn, PR #9: it locked the unit up), Hangman a guess answered, both the same samples with the bus answered FFh wherever the game does not read it; `TEST_GAMES_BREAK=1` answers FFh again (bl_board.h `bl_bus_break`) for run_tests' must-fail control. `--flash`: Simon put into the flash startup folder with a text file in one batch, as Load programs and files into flash puts them (bl_files_xfer.h `blx_add_cp`), and run from there; `TEST_GAMES_FILES_BREAK=7` writes its entry in folder 0 (the unit cannot find it) for that control. `../../../nvda/tools/files_add.py` checks `blazie_files add` itself. |

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
| `main_gtk.c` | The desktop shell, for Orca: one GTK 3 window -- the Windows app's menu bar, a keyboard area that takes keys down and up (chords and keys held through `bl_keys.c`, the Type 'n Speak through `tns_keys.h`) with an accessible name, the braille display field (Alt+Shift+B, as `brl_win.c`'s: the cells as Unicode braille for Orca, the bars on Alt+Shift+Right and Left), a status line announced to the screen reader, GTK dialogs and file chooser. Its units from `emu_catalog.c`; sound, settings and memory as `main_linux.c` (its settings text copied; `test_emu_gtk.py` checks it matches). |
| `test_emu_gtk.py` | The desktop app in Xvfb: keys through the X server, the window read through AT-SPI as Orca reads it (the braille display field's cells too); `BLAZIE_GTK_BREAK=noname`, `BLAZIE_GTK_BREAK=brlfrozen` and `BLAZIE_KEYS_BREAK=1` must fail. |

## Keys

**Braille Lite**, while the window is in front: F D S = dots 1 2 3, J K L = dots 4 5 6, the space bar = space, period or ; = the
advance bar, comma or A = the back bar. There are no cursor-routing keys and no dots 7 and 8: the Braille Lite 2000
has none. Every other key goes to Windows (Alt opens the menu). The keys can be changed in `blazie_emu.ini` beside the
program, section `[keys]` (`dot1=F`, ..., `space=space`, `advance=semicolon period`, `back=A comma`; punctuation by
name: comma, period, semicolon, apostrophe, slash, backslash, minus, equals, grave, lbracket, rbracket). Before 0.8,
A was a second advance bar.

**The braille display** (Braille Lite): **Alt+Shift+B** (or Settings > Braille display in the window; off until you
turn it on) shows the unit's 18 cells in a read-only field, which your screen reader shows on your own braille display
as the unit's real dots (Unicode braille; tried with NVDA; on Linux the desktop app does the same for Orca). Each
change is told to the screen reader (a name change event: NVDA redraws your display on it, saying nothing), and the
field has no name, so your display holds "ro edt" and all of the unit's cells. The field keeps the keyboard, so the
unit's keys work as before. Your display's own keys stay your screen reader's: they cannot press the unit's bars, and
the display does not advance by itself at the end of the field. From the PC keyboard: **period** = the advance bar,
**comma** = the back bar (and ; and A; **Alt+Shift+Right** and **Left Arrow** too where the screen reader leaves them
alone -- NVDA does; JAWS and Orca take them for their table navigation). Not the Type 'n Speak's: its keyboard is its
own. Turn off any braille translation your screen reader does: it can interfere with the unit's cells.

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

Settings > Double-speed upgrade installed (`[unit] double_speed=`, off by default): the Braille Lite's double-speed
chip was a $99 upgrade, not in every unit.  With it installed, the unit's own switch -- the status menu, then +
(dots 3-4-6), answered y -- runs its CPU at the crystal's 12.288 MHz, twice its usual clock
(`../../csrc/blazie/bl_board.h` `bl_set_double_speed`): the firmware answers keys sooner, while the speech, the
clock and the serial port keep their pace.  Without it the switch changes nothing, as on a unit without the chip.
Blazie's advice was to switch double speed off for programs that play sounds, and the emulator shows why, as Tomi's
unit does: the BASIC runtime writes some of a note's chip registers 1.5 us apart at double speed, too close for the
chip (`../../csrc/blazie/bl_board.h` `BL_LATCH_RACE_US`), so many notes come out as a low rumble and a program's
speech can cut off.  Pinball's occasional freeze on a real unit is not reproduced (since issue #25, whose fix keeps
the firmware's own speech clear of the race); Firmware > Restart the unit starts a unit that stops answering again.
test_emu_unit's "double speed" keys the English unit's switch with the upgrade and without (its control never
installs it).

Firmware > Restart the unit (keeps its files): what switching a real unit off and on does -- its memory saved, then
started again from it, the CPU from the beginning.  For a unit that has stopped answering; Back to the factory state
is the one that erases.

Firmware > Restart in one-handed mode (dot 6 held as it starts) and Restart in two-handed mode (dot 3 held as it
starts): the same power cycle, with the key held from the unit's first instant (`emu_hold_at_start`), as the manuals
say ("press a dot 6 as you turn on the unit"; the Braille Lite 2000/40 manual, 10.2, and the 1997 Braille 'n Speak
manual).  The firmware does the rest: it says "One-hand mode on" and keeps the mode in its memory, so the saved unit
starts one-handed until it is restarted two-handed.  In one-handed mode a character's dots go in a half at a time,
space entering it; a chord starts and ends with space.  Not offered for the Type 'n Speak, whose manual has no
one-handed mode (its firmware carries the words, from the code it shares with the Braille Lite, but nothing reaches
them that we know of).  test_clock's `onehand` checks it on the Braille Lite, the Braille 'n Speak 2000 and the 640.

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

The German and Italian Braille Lite 2000 (Blazie's September 20, 2000 updates, an anonymous donation) are in the
Firmware menu when `german\Lite2000.BNS` + `german\lite2000_de_fresh.state` or `italian\Lite2000.BNS` +
`italian\lite2000_it_fresh.state` are there; they keep their memory as `german.state` and `italian.state`. The German
unit answers a key a little later than the English one (~399 ms of chip time against ~247: its own rules). An English
unit can also be updated to German or Italian with Blazie's own update file (below), into its second bank.

The Braille 'n Speak 2000 is in the Firmware menu when `bns2000\BS03ENG.BNS` (English, June 24, 2003) or
`bns2000\BS2SLL.BNS` (Slovak) is there (Linux: menu numbers 15 and 16, `--unit bns-en`, `bns-sk`), each with its
factory state beside it, made from the firmware by `make_state.exe` (the three power-ons that made the English Braille
Lite's state, three seconds; `firmware/blazie/README.txt` has the commands and hashes):

    make_state bns2000\BS2SLL.BNS english bns2000\bs2sll_fresh.state

**More languages.** Blazie's Braille Lite 2000 updates in other languages -- the releases the emulator knows, listed
in `src/csrc/blazie/bl_languages.txt` -- appear under Firmware > More languages when their .BNS file is anywhere in
the firmware folder (or one folder down; found by its content, not its name), and only then. The first time one
starts, it sets itself up from its own firmware, answering its setup questions in its own language (a few seconds:
"Setting up the unit for its first start"); after that it keeps its memory as `<its id>.state`, as the others do.
On Linux they follow the eight in the terminal menu (20 on) and take `--unit` with their id (`bl2_hu_1999_07`).

## The Braille 'n Speak 2000

The Braille Lite 2000 without its braille display, on the same board (`../../csrc/blazie/bl_board.h`, `bl_model`).
Run side by side through the same keys at the same instruction counts, its firmware and the Braille Lite's use the
same ports with the same values -- the keyboard, the SSI-263, the memory and flash banks, the power bits, the battery
gauge, the clock controller -- run the same memory test, keep the same file system in the same 2 MB flash, and ask
the same questions at the same points; only the display's traffic on the 8255 is missing.  The board tells the two
apart by the firmware's idle loop.  The Slovak unit speaks Slovak through the same chip (its own letter-to-sound
rules: a key's answer takes ~443 ms of chip time, the English ~247); it takes the date in another order than the
English unit's month, day, year (those digits set October 9).

## The Braille 'n Speak 640

The board again, with an OKI M6242B real-time clock at ports 60h-6Fh in place of the clock controller
(`../../csrc/blazie/bl_rtc6242.h`, from OKI's data sheet); the board tells it by its firmware's code.  Without the
chip its firmware waited forever for the clock to stop being busy, right after "Braille 'n Speak ready".  Its
releases are the emulator's only (`bl_languages.txt`): Blazie's June 2003 English (`bns640.exe`, its BSPENG.BNS), and
from a 640's flash after its last repair (issue #15) the September 2000 English and the Polish ("gotowy, pomoc
otwarta"; its keys answer in ~380 ms of chip time, its rules slower, as the Slovak 2000's).  It has no file flash:
its documents live in its battery RAM (183 pages free from new, as the 2000s say before their "2045 K flash"), so
the flash half of the file tools does not apply to it.  Its clock starts from this PC's, is set with its own
commands, and is kept in the saved state with the time the unit was off; it says the year as it is (2027 as twenty
twenty-seven: its firmware's own century; its date command takes years up to 2089), so the Braille Lite's 28-year mapping (below) is not its.
The download carries all three in `firmware\bns640\` (0.8.5: `BSPENG.BNS`, `BSPENG_2000.BNS`, `BSPPLA.BNS`, one
folder down where the emulator finds them); each sets itself up on its first start, as the other languages do. They
are in the emulator's downloads only, never in the phone apps.

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
"ready". The units' RAM is 768 KB (40000h-FFFFFh: the firmware sizes it at power-on and gives files RAM up to its
top). A Braille Lite state is 768 KB (the RAM's first 256 KB + the flash's first 512 KB) while the rest of each is
empty -- every factory state -- and keeps all of the RAM (768 KB) or all of the flash (2 MB) once files reach it; every
size loads. Before this, a state kept only the RAM's first 256 KB, and files the unit had put past 7FFFFh were lost
when it was saved.

## The firmware update

Since 0.8 the Braille Lite 2000 takes Blazie's own firmware updates, as Blazie's update guide gives them. Its program
lives in a second flash chip, the "super flash": two banks, each a whole firmware, p-chord l switching between them.
Put the update program (a `.bns`, such as `lite2000.bns`) into the unit's files -- over the serial port with YMODEM,
or with Files > Import -- and run it with o-chord x, its name, e-chord, from the folder it is in (a file in flash
needs "allow folder mode", status menu item 43, to move into the flash folder first). It speaks its own language:
answer its questions, and when it asks which bank, b keeps your first language as it was (Blazie's advice). It checks
the file for about 30 s, then erases the bank and programs it, clicking every few seconds, about two minutes; a steady
tone means success (rapid beeping would mean it failed). Then close the program and start it again -- the unit, like
a real one switched off and on, starts in bank a -- and p-chord l switches to the new firmware. Your files are kept.

The emulator keeps the updated bank beside the unit's memory, as `english.state.program` (or `spanish`), and puts it
back each time it starts that unit; Back to the factory state removes it. Only an update into bank b is kept: the
unit's first bank must stay the firmware the emulator loads.

## The owner's name

Blazie's name brander (`bsname.bns`, with its `bsname.txt`) writes a name and address, 39 characters, into the
program flash once: load it into the unit's files, run it with o-chord x, type the name letter by letter (no Grade 2)
and press e-chord. p-chord th then says "owner is" and the name in any file. On a real unit the name sits beside the
serial number at the start of the firmware's bank (00080h-000B7h, blank in Blazie's images), so a cold reset, which
clears the files and settings, leaves it, and Blazie's updater copies it into the bank it writes. The emulator
keeps it the same way: in `english.state.program` beside the unit's memory, put back each time it starts that unit
(before this, it was gone on the next start). Back to the factory state removes it with the rest.

## Licence

MIT (`LICENSE` beside the program), so it can be ported (to another screen-reader platform, a Linux device, ...),
with Casso's MIT notice (`licenses/Casso-MIT.txt`: the chip model draws on it). The boards run on MAME's Z180 core
(`../../csrc/cpu/z180_mame.cpp`), which keeps its BSD-3-Clause licence (`licenses/MAME-Z180-core-BSD-3-Clause.txt`).
`build_app.py` puts all three beside the program. Since 0.7 no z180emu (GPL) is in the program or its tests:
`python tools/check_no_gpl.py nvda/dist/blazie-emu` (run_tests runs it). The firmware keeps its own terms and is not
part of the program.

## Programs and files into flash

Firmware > **Load programs and files into flash** puts Blazie's programs and games, BASIC programs, text documents
and braille files into the unit's flash startup folder, with no image and no cable. The dialog first shows `.bns`,
`.bas`, `.txt`, `.brl` and `.brf` files (programs only, and all files, are its other filters). Choose one or
several files together (Shift or Ctrl with the arrows and Space, or Ctrl+A), and they go in as one batch -- the
unit is switched off once, the files added, and switched on again, as Import does (and, like Import, only while the
unit is not writing its flash). Each file gets the type the unit gives its name: `.bns` a program, `.brl` (and no
extension, on the Braille Lite) grade 2 braille, the rest text. Then, with the unit's own commands: run a program
with o-chord x, its name, e-chord; open a document with o-chord f o, its name, e-chord (the Type 'n Speak: F9 x and
F1 o, its name, Enter). No folder mode is needed for files in flash startup (tried on the English Braille Lite 2000,
the Braille 'n Speak 2000 and the Type 'n Speak: Simon runs from there as from RAM).

- All or nothing: if one file cannot go in -- the flash is full (the message says how much is free), a name the unit
  cannot take, an empty file, two files that would have one name on the unit -- nothing is loaded.
- A name the unit has already: the emulator lists them and asks once for the whole batch; Yes rewrites each where it
  is (its folder, type and protection kept), No loads nothing. The unit's clipboard and datebook, and the file it
  has open, are never replaced.
- Names become names the unit takes (lower case, one dot, an extension of up to 3, 20 characters), as Import makes
  them; a text file's PC line ends become the unit's CR, and a program (it holds NUL bytes) is kept byte for byte.
- The unit as it was is kept beside its state as `english.state.before-import` (...), as Import keeps it.

The same from the command line, on a saved unit (close the emulator first):

    blazie_files add english.state pinball.bns chess.bns story.brl [--replace] [--dry-run]

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
                                                        state as english.state.before-import; --keep-exact:
                                                        text files keep their line ends, LF or CR LF, instead
                                                        of being made the unit's CR)
    blazie_files extract english.state myfiles --crlf  (each text file as a PC's, CR LF; --lf: Linux's, LF.
                                                        A file with a NUL byte -- a compiled BASIC program --
                                                        is binary and kept as it is, as add and import keep it)

**A Type 'n Speak that was never set up** (saved by the 0.6 or 0.7 previews, or a setup question answered n: "The
Type 'n Speak's first start" above): the import refuses ("the unit's folders were never set up"); export works. The
program offers to set it up, keeping its RAM files, when it starts it.

## Not yet

- An Android shell (the portable files are ready for it). The Linux shells are `main_linux.c` (a terminal) and
  `main_gtk.c` (a GTK window, for Orca): `README-linux.md`.
