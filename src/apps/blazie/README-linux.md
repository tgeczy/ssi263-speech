# The Blazie emulator on Linux

A Braille Lite 2000 or a Type 'n Speak in a terminal: Blazie's own firmware on the emulated board, with the emulated
SSI-263 as its voice -- the same unit as the Windows app (`emu_unit.c`), on a Raspberry Pi's console, a desktop's
terminal, or a BTSpeak, Blazie Technologies' Linux notetaker on ARM: the old Blazie units running on the new one.

On a BTSpeak or BT Braille it reads the device's own braille keys and shows the Braille Lite's display on the BT
Braille's: [README-btspeak.md](../../platforms/btspeak/README-btspeak.md).

It boots to the unit's own main menu and behaves as the unit does. What you type goes to the unit as its keys; what
the program itself says (its menu, its messages) is plain lines of text, which the console's screen reader reads
(Speakup, Orca, BRLTTY's speech on the BTSpeak).

## Build

    sudo apt install build-essential pkg-config libasound2-dev
    ./build_linux.sh

`build/linux/blazie_emu`: one program with the C++ runtime inside; it needs only the C library, the maths library and
ALSA's `libasound` (on every Linux with sound). It is built with MAME's Z180 core (BSD-3-Clause) and the project's
own MIT code only -- never z180emu -- so it is MIT, with MAME's notice for the Z180 core; `tools/check_no_gpl.py`
checks the program and the package. Without ALSA's headers the build uses PulseAudio's simple API when `libpulse-dev`
is there (or ask for it: `BLAZIE_AUDIO=pulse ./build_linux.sh`); with neither, the emulator is not built and
`tools/linux_tests.sh` fails saying so.

The Linux package (`tools/package_linux.sh`) carries it as `bin/blazie_emu` with this file as
`README-blazie-emu.md`, the firmware in `share/ssi263-speech` (the Type 'n Speak's in its `tns` folder), and the
licences (`LICENSE`, `licenses/MAME-Z180-core-BSD-3-Clause.txt`, `licenses/Casso-MIT.txt`). Run it from the unpacked
package (`./bin/blazie_emu`), or `sudo ./install.sh`, which also puts it in `/usr/local/bin` with its firmware.

The desktop app, `build/linux/blazie_emu_gtk` (below, "The desktop app: a window for Orca"), is built too when GTK
3's headers are there (`sudo apt install libgtk-3-dev`); without them the build says it skipped it, and the terminal
`blazie_emu` is built as before (a BTSpeak has no desktop). GTK is LGPL-2.1-or-later and is linked dynamically, as
the system's own library: it is not in the package, and the program stays MIT (`tools/check_no_gpl.py` checks it
too).

**Which Linux.** A program built on one distribution runs on that one and newer: the package's 0.7 builds come from
Debian 13 (glibc 2.38 or later). On an older system -- quite possibly a BTSpeak, whose system is Raspberry Pi OS of
some version -- build it on the machine itself (the three commands above), which also covers a 32-bit (armhf) system.

## BT Speak and BT Braille

Run `blazie_emu`; on a BT Speak or BT Braille it uses the device automatically. It says "BT Speak or BT Braille
detected: using its keyboard, speech and braille display." and hands over to `blazie_emu_bt` beside it, the BT front
end (#4, Leo), with the device's own dialogs for its menus, direct six-dot keyboard input and the firmware's braille
display (`bt_handover.c`; `--unit`, `--firmware`, `--config` and `--rate` are passed on, `--config` as its
`--state-dir`). The device is recognised when the system's `python3` imports the device's `BTSpeak` library and its
keyboard service answers; an ordinary PC or Raspberry Pi has no `BTSpeak` library, so `blazie_emu` stays in the
terminal there (`blazie_emu --bt-probe` says what it finds). See [the BT front end's guide](../../platforms/btspeak/README.md),
or `README-blazie-bt.md` in the package.

`bt = native` under `[input]` in the settings keeps `blazie_emu` itself on the device, with no Python needed: it
takes the keyboard from the device's keyboard server and shows the unit's display on any braille display BRLTTY
drives, its menu as plain lines BRLTTY reads ([README-btspeak.md](../../platforms/btspeak/README-btspeak.md)). It does
the same when `blazie_emu_bt` is not installed beside it (one line says so). `--no-bt`, or `bt = off`, leaves the
device's keyboard and display alone: the terminal emulator, as everywhere else. The desktop app (`blazie_emu_gtk`)
never uses them: the BT devices have no desktop.

`blazie_emu_bt` is built alongside `blazie_emu` and `blazie_emu_gtk`; the Linux release includes it and its worker
`bin/blazie_bt` (keep both). It needs Python 3.11+ and the device's BTSpeak libraries, and keeps its memory and
preferences in the BT user directory (unless `--config` is given).

## Run

    blazie_emu                    the unit you used last (the first time: the English Braille Lite)
    blazie_emu --unit tns-en      bl-en, bl-es (the Spanish Braille Lite), tns-en, tns-es (the Type 'n Speak),
                                  bns-en, bns-sk (the Braille 'n Speak 2000, English and Slovak)
    blazie_emu --show-keys        what this keyboard sends: the terminal's bytes and the keys they are, and the
                                  input devices' keys going down and up (for the key settings; q q stops it)
    blazie_emu --no-sound         no sound card: the unit runs on silent, paced by the system clock
    blazie_emu --no-bt            on a BT Speak or BT Braille: the terminal emulator, the device's keys left alone
    blazie_emu --bt-probe         is this a BT Speak or BT Braille? (yes: exit 0)
    blazie_emu --help

The firmware is looked for in `firmware_dir` in the settings, else beside the program: the package's
`../share/ssi263-speech`, a `firmware` folder, or the source tree's `firmware/blazie` (run from `build/linux`); or
give it: `--firmware DIR`. Either layout works: the repository's (`BL2ENG.BNS`, `spanish/`, `tns/`, `bns2000/`) or
all in one folder.

**F11 opens the menu** (and Ctrl+O for the Braille Lite; Alt+Shift+F always, as on Windows). Type a number and
Enter; Enter alone goes back to the unit:

| | |
| --- | --- |
| 1-4 | The unit: Braille Lite 2000 English or Spanish, Type 'n Speak English or Spanish |
| 5 | Back to the factory state (erases this unit's files; type yes) |
| 6 | Sample rate, 11025-48000 Hz (the unit restarts at it, its memory kept) |
| 7-10 | The Braille Lite's idle channel: the sound (as the unit, hiss, whine, silent), keeping it open (off, until the unit clicks off, always), the pop and click, the 10 Hz tick -- as on Windows (README.md) |
| 11 | Quick key response (faster than the real unit) |
| 12 | The serial port (below) |
| 13 | The Braille Lite's keyboard: keys or letters (below) |
| 14 | The keys, in short |
| 15, 16 | The Braille 'n Speak 2000, English and Slovak (listed when its firmware is there: README.md, "Firmware"); the Braille Lite's keys |
| 17 | The sound buffer: automatic, medium or long (below, "Sound") |
| 0 | Exit: the unit's memory is saved |

After a choice the menu says one line; `?` lists it again.

## The desktop app: a window for Orca

`blazie_emu_gtk` is the same emulator in a GTK 3 window, for a Linux desktop and its screen reader, Orca -- what
`blazie_emu.exe` is on Windows with NVDA. The package carries it as `bin/blazie_emu_gtk`; `sudo ./install.sh` puts it
in `/usr/local/bin` with a menu entry, "Blazie emulator" (Utility, Accessibility).

    blazie_emu_gtk                  the unit you used last
    blazie_emu_gtk --unit tns-en    bl-en, bl-es, tns-en, tns-es, bns-en, bns-sk
    blazie_emu_gtk --no-sound       no sound card: the unit runs on silent
    blazie_emu_gtk --firmware DIR   --config DIR, as blazie_emu

It shares the terminal program's settings file and memory folder (below): a unit saved by one starts in the other.
Run one at a time (each saves its unit when it closes).

**The window**, top to bottom:

- **The menu bar**, the Windows app's item for item, with its mnemonics: Firmware (Alt+F: the four units -- six
  when the Braille 'n Speak 2000's firmware and factory states are there; the Slovak one's file names go in and out
  in code page 852 -- Export
  files to disk image (.img), Import files from disk image (.img), Back to the factory state, Exit), Settings (Alt+E:
  the idle channel, keep the channel open, the pop and the click, the 10 Hz tick, quick key response, Sample rate,
  Sound buffer (below, "Sound"), Serial port: none, the serial devices present, or a pseudo-terminal), Help (Alt+H: Keys, About). Ctrl+Q exits
  (with the Braille Lite; on the Type 'n Speak Ctrl+Q is the unit's).
- **The keyboard area**, the one thing in the window that takes the focus. When it does Orca says its name, "Braille
  Lite 2000 (English): keyboard. F11 or Alt+Shift+F opens the menu", and "panel".
- **The status line**: short results -- "Switched to the Type 'n Speak (Spanish).", "Exported 12 files to ...",
  "Sample rate 22050 Hz." -- which Orca speaks as they come (ATK's announcement: ATK 2.46 and later, as on Debian 12
  and 13; with an older one the line is only there to read, with Orca's flat review).

Questions and longer reports are GTK message dialogs, which Orca reads as they open (each named for what it is
about: "About", "Type 'n Speak (English)", "Import files"): the Type 'n Speak's first start and its seven questions,
the offer to set up a Type 'n Speak the previews saved (Set it up, keep its files / Factory state / As it is), Back to
the factory state, the import's report and its question before deleting, Keys, About. Export and import choose the
image in GTK's own file chooser; the export's result is said on the status line. The memory is saved as the other
shells save it: every minute, when the window closes (Exit, or the window manager's close), on switching units, when
the desktop session ends (GTK's query-end), and on SIGTERM or SIGHUP.

**Keys.** GTK says when each key goes down and when it comes up, so the keys are as on Windows (and as the input
devices in a terminal) -- no hold key:

- **Braille Lite**: F D S = dots 1 2 3, J K L = dots 4 5 6, the space bar, ; = the advance bar, A = the back bar
  (the Braille Lite 2000's two advance bars; `[keys]` `dot1` .. `advance`, `back` in the settings). A chord goes to the unit when its last key comes up, and the keys held down
  are held on the unit while you hold them: p-chord, l, then hold i-chord at once -- the cold reset ("initialize file
  system?"). Every other key is the program's: Alt and a letter for the menus, Tab, and so on.
- **Type 'n Speak**: the whole keyboard is the unit's (`tns_keys.h`, the one table Windows and the terminal use),
  Alt, Ctrl, Tab, Escape, F10 and the other function keys included; each key goes down and comes up as you move it.
- **F11 or Alt+Shift+F** opens the menu whichever unit runs (the keys held are let go first; `[keys] menu` and
  `tns_menu` set others); Escape closes it and gives the keys back to the unit. F10, GTK's usual key for a menu
  bar, is not one here: it is a Type 'n Speak key.
- The keys are taken by their place on the keyboard, not by what a layout prints on them (X's and Wayland's key
  codes are Linux's input codes, which `evdev_linux.c` names): on an AZERTY keyboard the Braille Lite's dots stay
  under the same fingers, and the Type 'n Speak's keys are where a US keyboard has them.
- Nothing is grabbed. Only while the keyboard area has the focus do keys reach the unit; when the window loses the
  focus (Alt+Tab, a dialog), a chord half pressed is dropped and the keys held come up.

**With Orca.**

- Orca's own keys stay Orca's: the Orca key (Insert in Orca's desktop layout, Caps Lock in its laptop layout) and
  the keys pressed with it never reach the unit. So the Type 'n Speak's Insert (desktop layout) or Caps Lock (laptop
  layout) is Orca's; Orca's "pass the next key through" (Orca+BackSpace, then the key) sends it to the unit.
- Orca's key echo, if on, speaks each key over the unit's own voice; most people will want it off while using the
  unit (Orca's Preferences, Echo).
- The unit's voice and Orca's share the sound card (through PulseAudio or PipeWire on a desktop) and can speak at
  once.

## Not yet tried with a real Orca user

The desktop app was tested under a virtual display (Xvfb) with the accessibility bus that Orca reads (AT-SPI): the
names, roles, focus and announcement events Orca gets were checked, but no one has yet used it with Orca speaking.
To find out with a real user:

- what Orca says when the window opens, when the keyboard area takes the focus, and when a dialog closes and the
  focus comes back (the tests see the focus return and the area named; Orca's words are untried);
- whether Orca speaks the status line's announcements (the event was seen on the bus, sent with ATK 2.56's
  "notification" signal; the older "announcement" signal of ATK 2.46-2.49 was not exercised, and an Orca older than
  the announcement event may say nothing);
- Orca's laptop layout with the Type 'n Speak (Caps Lock), Orca+BackSpace to pass a key through, and Orca's key echo
  over the unit;
- a real window manager (the tests had none): Alt+F4, Alt+Tab away and back with a chord or a Type 'n Speak key
  held, the dialogs over the window;
- a Wayland desktop (GNOME's default; the key codes are the same, but not tried), and an x86-64 machine (tested on
  arm64 only, a Raspberry Pi 5 with Debian 13);
- sound actually heard on a desktop (the tests run with --no-sound), alongside Orca's voice.

## The settings and the units' memory

`~/.config/ssi263-speech/blazie-emu/` (or under `$XDG_CONFIG_HOME`; `--config DIR` for another):
`blazie_emu.ini`, written the first time with every key setting explained, and each unit's memory --
`english.state`, `spanish.state`, `tns_english.state`, `tns_spanish.state` -- saved when you exit, when you switch
units, and every minute (written whole, then put in place: a power cut never leaves half a file). The first time,
the Braille Lite starts from the shipped state and the Type 'n Speak as a new unit: its own cold reset (Ctrl+Alt+Del
held at power-on) asks how to set itself up, and the program says so. Press y for each question (the Spanish unit:
s), seven times: initialize file system, are you sure; initialize flash system, are you sure (then the flash chip's
~46 seconds of clicks); initialize folder system (it says it is ready and opens its help); delete all data in file
area, are you sure (then about 35 seconds of silence while it clears its memory, and it starts again). README.md,
"The Type 'n Speak's first start", says why each one matters.

A Type 'n Speak saved by the 0.6 or 0.7 previews was never set up (their first start missed the unit's cold reset:
a new file lost its first letter, a file moved to flash was lost). When such a unit starts, the program says so and
asks: `k` sets it up now and keeps its RAM files (its settings go back to the factory's), `f` starts it from the
factory state (its own questions again), Enter alone starts it as it is. `k` and `f` keep the old memory beside it
as `tns_english.state.before-setup`.

`blazie_files` (in the package's `bin`, beside `blazie_emu`) lists, exports, imports, extracts, packs and unpacks a
saved unit's files from the command line, as on Windows (README.md, "Files in and out"); close the emulator first.

## Keys

### Braille Lite: three ways in

A terminal says when a key goes down, never when it comes up, and BRLTTY (the BTSpeak's keyboard driver) hands a
program characters, not keys. So the program takes the Braille Lite's six keys, space bar and advance bars three ways
(and a fourth on a BTSpeak or BT Braille, the device's keyboard server: README-btspeak.md):

**Keys mode** (`[keys] mode = keys`, the default; a PC keyboard in a terminal): F D S = dots 1 2 3, J K L = dots
4 5 6, the space bar, ; = the advance bar, A = the back bar. The keys you type close together are one chord: it goes to the unit
80 ms after the last (`chord_ms`). Holding a chord down does not repeat it (a key typed again within 150 ms,
`repeat_ms`, is the keyboard's auto-repeat). The keys are set in `[keys]` (`dot1 = f brl_dot1` ...).

**Letters mode** (`[keys] mode = letters`, or menu 13; the BTSpeak's own braille keyboard, or any braille display's
keyboard through BRLTTY): each character typed is its braille cell, in computer braille (North American Braille
Computer Code, which BRLTTY's US tables type): `p` is dots 1 2 3 4, `4` is dots 2 5 6, space is the space bar.
Each chord goes to the unit at once. Chords with the space bar, which BRLTTY keeps for itself:

- the chord prefix, **Ctrl+C**, then the character: Ctrl+C p is p-chord (`[letters] prefix`);
- a **capital letter** -- dot 7 with the letter on a BTSpeak -- is the letter's chord: `P` is p-chord
  (`capital_is_chord = 1`);
- the keys BRLTTY makes of chords, mapped back to them, as the BTSpeak's own table (its user's manual, "Basic
  Keyboard Navigation"): Up = dot-1 chord, Down = 4, Left = 3, Right = 6, Ctrl+Left = 2, Ctrl+Right = 5,
  Page Up = 2-3, Page Down = 5-6, Home = 1-3, End = 4-6, Ctrl+Home = 1-2-3, Ctrl+End = 4-5-6, Tab = 4-5,
  Shift+Tab = 1-2, Insert = 3-5, Delete = 2-5-6, Esc = 2-6 chord; and Enter (dot 8) = e-chord, Backspace (dot 7) =
  b-chord, the unit's own Enter and Backspace; Ctrl+A = the advance bar, Ctrl+B = the back bar. Each is a line in
  `[letters]`, key = chord: `up = 1-chord`, `f9 = dots 1 3`, `ctrl-a = advance`, `delete = none`.

**The advance bars.** The Braille Lite 2000 has two, advance and back, which its firmware reads on its own port (not
with the chords): pressed from a terminal a bar is tapped -- held long enough for the unit to see it -- and typed
with a chord's keys it is the unit's bar-and-chord command; from an input device it is down as long as it is held.
Before this, the emulator's advance bar went where the firmware never looks, and did nothing.

**Input devices** (`[input] evdev`): Linux's `/dev/input` devices report each key going down and coming up, so the
chords work as on Windows -- the chord when the last key comes up, and the keys held down seen as held. `auto` (the
default) uses them on a text console, when a keyboard can be read; `on` everywhere (on a desktop, mind that they are
read whichever window is in front); or a device's path. Reading them needs the `input` group: `sudo usermod -aG input
$USER`, then log in again. With `grab = 1` (the default) only this program gets those keys while it runs -- not the
console, not a screen reader -- except while its menu is open; the kernel lets go if the program ends. A braille
keyboard that Linux reports with its own dot keys (`brl_dot1`..`brl_dot8`) is mapped too. A keyboard another
program holds for itself (BRLTTY can) is left alone and named at the start ("held by another program"): its keys
come through the terminal as characters, so letters mode is the way in there. With the devices grabbed, keys that
still reach the terminal can only be another keyboard's, and go to the unit too.

### Keys held: the hold key

The Braille Lite reads the keys held down while it starts -- when it is switched on, and when p-chord, l restarts
it: i-chord held is the cold reset ("initialize file system?"), all seven keys the warm reset (README.md, "Keys held
while the Braille Lite starts"). The input devices see keys held as they are. From a terminal, the **hold key**
(F12 or Ctrl+K, `[keys] hold`) does it: press it, and the next chord typed is held down instead of sent, until you
press it again -- then it comes up and is sent, as the keys coming up would be. A chord typed meanwhile is sent as
usual. The cold reset: p-chord, hold key, i-chord, l -- the unit restarts with i-chord held and asks "initialize
file system?" -- then the hold key again. The program says "Holding i-chord" and "Let go".

### Type 'n Speak

The whole keyboard is the unit's (its key codes: `tns_keys.h`, the same table as Windows'). From a terminal each
key is pressed whole -- the modifiers it needs down, the key down and up, the modifiers up: `A` is Shift and a,
Ctrl+O is Ctrl and o, Alt+X is Alt and x, `!` is Shift and 1 (a US keyboard's shifted characters). From the input
devices each key goes down and up as you move it. F11 or Alt+Shift+F opens the menu (`[keys] tns_menu`).

## Sound

ALSA's `default` device (`[sound] device` in the settings for another: `hw:0`, `plughw:1`, ...), 16-bit mono at the
unit's rate (44100 by default), in blocks of 10 ms (`[sound] block_ms`, 5-20). A thread renders each block as the
card drains one, so the card's clock paces the unit; it asks for real-time priority and runs without it. On a
desktop, ALSA's default device reaches PulseAudio or PipeWire through their ALSA plugin. With no sound card (or
`--no-sound`) the unit runs on, silent, paced by the system clock.

**The sound buffer** (menu 17; Settings > Sound buffer in the desktop app; `[sound] buffer=` in `blazie_emu.ini`,
the same key and values as the Windows app's, `audio_pace.h`): how much sound is kept queued at the card. A key's
speech plays behind it; too little, and the speech breaks up whenever the program is held up (Tomi: the emulator's
speech stutters, the add-on's doesn't).

| Choice | Queue | |
| --- | --- | --- |
| `auto` (the default) | 60 ms, growing (100, 150, 220, 250 ms) each time the card runs dry, for the session | |
| (`short`) | 40 ms | no longer offered: it broke up on a ROG Ally played directly, and 50 ms did too; a settings file's `short` now reads as `auto` (kept in the table below as the old queue it was measured as) |
| `medium` | 100 ms | |
| `long` | 250 ms | **recommended when the unit shares the sound device with a screen reader** (the BTSpeak's own voice, Speakup, Orca, through PulseAudio or PipeWire), and over a remote session |

The device's buffer is opened for the longest queue (300 ms) and the thread keeps only the chosen queue in it, so a
new choice, or the automatic queue growing, takes effect at the next block, without reopening the device. On each
block the thread asks ALSA how much is still queued (`snd_pcm_avail`: what is in ALSA's buffer; a sound server's own
latency comes on top) and how far the card has played (what was written less `snd_pcm_delay`); the card running
dry is told, as on Windows, by the played position falling behind the wall clock (`audio_pace.c`, `ap_observe`). The
PulseAudio build (`BLAZIE_AUDIO=pulse`) has only the stream's whole latency (`pa_simple_get_latency`), so there the
queue counts the sound server's latency too: choose `long` with it on a Bluetooth or remote sink.

Measured on a Raspberry Pi 5 (arm64, Debian 13) playing to ALSA's dummy card (`snd-dummy`, `plughw:Dummy`; it
plays in real time from the system clock, no speaker), 30 s each, the Braille Lite running from its boot. The sound
thread held up before 5% of its writes by up to the time given (`BLAZIE_EMU_AUDIO_STALL`, as on Windows: the same
hold-ups in every run, its random numbers never seeded, so each queue met the same ones); the sound
lost by the card's played position against the wall clock, its underruns as ALSA counted them; "ahead": how long
until a block just written is heard, which a key's speech waits for (plus up to a block until the unit takes the
key):

| Sound buffer | not held up | held up to 30 ms | held up to 60 ms | ahead |
| --- | --- | --- | --- | --- |
| short (40 ms) | 0 | 188 ms lost in 13 gaps (14 underruns) | 1.8 s lost in 51 gaps (59 underruns) | 36-38 ms |
| auto (60 ms) | 0, stays at 60 ms | 21 ms in 1 gap, grew to 100 ms | 25 ms in 2 gaps, grew to 150 ms (75 s: 34 ms, 2 gaps) | 57-58 ms; 119-133 averaged over the runs that grew to 150 ms |
| medium (100 ms) | 0 | 0 | 85 ms in 4 gaps | 93-98 ms |
| long (250 ms) | 0 | 0 | 0 | 242-247 ms |

With all four cores busy (`stress-ng --cpu 4`) and the thread not held up, no gap at any setting. The minute's save
(1.3 ms here) ran on the sound thread with the queue rendered 50 ms ahead, without a gap. `BLAZIE_EMU_AUDIO_LOG=file`
logs the gaps found, the saves and, when the program ends, the gaps, the sound lost, the underruns and how far ahead
the sound was; `test_audio` (built here too, and run by `tools/linux_tests.sh`) runs this thread's queue against a
simulated card, with the 0.7.0 draft's four blocks as its must-fail control.

On a Raspberry Pi 5 the running unit takes about a fifth of one core at 44100 Hz (measured with ALSA, PulseAudio
and silent). A BTSpeak's Compute Module 4 is two to three times slower: if the sound breaks up there, choose the
long sound buffer (menu 17), 22050 Hz (menu 6), or `[sound] block_ms = 20`.

Saving the unit's memory holds the unit while the file is written (the Type 'n Speak's is 5 MB). The minute's save
runs on the sound thread once it has rendered 50 ms beyond its queue, so the card plays that while the file is
written (Tomi: the emulator's speech stutters, the add-on's doesn't); a save slower than the queue in hand (on slow
storage) can still be heard as a short gap, and the automatic queue then grows. Saves on switching units and on exit
are written from the program's main thread, as before, under the same lock: the two never write the same file at
once.

## The serial port

Menu 12: a serial device (`/dev/ttyUSB0`, `/dev/ttyACM0`, `/dev/ttyS0`, `/dev/ttyAMA0`; the menu lists the ones
present; the `dialout` group is needed: `sudo usermod -aG dialout $USER`) or `pty`, a pseudo-terminal whose other
end a program on the same machine opens: the program says its name and links it as
`~/.config/ssi263-speech/blazie-emu/serial`. A terminal program (`picocom`, `minicom`), or a DOS disk tool under
DOSBox with its serial port on that device, then talks to the unit. The line follows the firmware as on Windows
(`serial_win.c`; what the firmware does: `../../csrc/blazie/bl_serial.h`): the rate, data bits and parity it
programs, set in step with its bytes (the storage commands switch to 19200 and back), DTR while its port is on, RTS
for its handshake line, its XON and XOFF passed through as bytes (the tty's own flow control is off). The choice is
kept in `[serial] port` and used again next time; a port missing then leaves the unit unplugged, the setting kept.

## Tests

`tools/linux_tests.sh` runs, on the Linux machine (no sound card, no firmware in the repository: a firmware folder
is given):

- `test_keys` -- the keyboard without a unit: a terminal's sequences (xterm, VTE, the Linux console), keys mode's
  chords by time and its auto-repeat, the hold key, letters mode's computer braille and mapped keys, an input
  device's keys down and up, the Type 'n Speak's strokes, the settings file. Its control (`BLAZIE_KEYS_BREAK=1`,
  dots 1 and 4 swapped) must fail its Braille Lite checks and only those.
- `test_emu_unit`, `test_clock` -- the unit headless as on Windows, here on MAME's Z180: the boot, a chord answered,
  key latency, saving; the Braille Lite's display (its cells after the boot) and its two advance bars panning it
  forward and back (the Braille 'n Speak: no display), whose control (`TEST_EMU_BARS_BREAK=1`, the bars sent where
  the shells used to send them) must fail the panning checks; the clock controller, the date and time set and read with the units' own commands, i-chord
  held through a restart (its control drops the held keys and must fail).
- `test_rescue` -- a Type 'n Speak made as the previews made it (no file system, no folders; a file moved to flash
  lost) is told apart and set up anew with its RAM files; its control leaves the old cold start on and must fail.
- `tools/blazie_files_roundtrip.sh` -- `blazie_files` on a copy of the shipped state: export, unpack, a new file,
  pack, import; the unit lists it and every old file is unchanged. Its control (`TEST_FILES_BREAK=2`, a new flash
  file's blocks left unmarked) must fail.
- `test_emu_linux.py` -- the whole program headless (`--null`, keys typed from a script at their times through the
  terminal's decoding): the Braille Lite boots and answers F (not without it); o-chord t typed as keys and as letters
  says the host's time; the Type 'n Speak from cold, its seven setup questions answered y, F4 says the time; the date
  and time set through the unit's
  commands, saved to the memory folder and started from again; i-chord held through a restart by an input device's
  keys; and the program run as a person runs it, in a pseudo-terminal, its serial port on another: s-chord's XON ENQ
  at 19200 bit/s, ACK answered with 'C', NAK (the control) not; and menu 17, the sound buffer chosen long, written to
  the settings and shown so the next time. Its control (`BLAZIE_KEYS_BREAK=1`) must fail the two clock checks.
- `test_btkb` -- the BTSpeak's and BT Braille's keyboard server (`btkb_linux.c`) against a server of the test's
  own: the hello asking for the keys alone, every key consumed within the server's 10 ms while the program is busy,
  the keys in order, a server that is busy or absent or goes away; #4's gestures (M-chord or Z-chord with dot 7)
  and the bars by time; the panning keys from BRLTTY's tables; the display's layout. Its control (`BTKB_BREAK=1`,
  dots 7 and 8 never waiting for a chord's other keys) must fail the gesture typed 7 first.
  `test_emu_linux.py`'s `btspeak` runs the whole program with such a server.
- `test_audio` -- the sound buffer (`audio_pace.c`, as on Windows) against a simulated sound card: for the Windows
  shell's thread and this one's (the card asked how much is queued and how far it has played, the thread asleep
  until a block is wanted): a steady card, a busy machine, a remote card, a slow save; and the arithmetic alone (the
  frames queued in blocks, the played position from ALSA's delay, the sleep, the buffer opened). Its control
  (`--old`: the 0.7.0 draft's four blocks, the save on the main thread) must fail the busy, remote and save checks of
  both threads, and only those.
- `test_emu_gtk.py` -- the desktop app in a virtual X display (`xvfb-run`) with its own session and accessibility
  buses (`dbus-run-session`, at-spi2-core), its keys typed through the X server (xdotool: GDK's own key events, down
  and up), its window read through AT-SPI as Orca reads it (python3-gi, gir1.2-atspi-2.0); `--no-sound`, and
  `--trace` tells the test the unit's level, its keys and its status lines. It checks: the settings file it writes is
  the terminal shell's, word for word; the menu bar's items by name; the keyboard area focused, with its name and
  role; the boot and F (dot 1) answered; Export through GTK's file chooser, the image written and the result
  announced; F11 and Alt+Shift+F opening the Firmware menu, Escape giving the focus back; Help > About's text read
  from its dialog; switching units announced and the area renamed; the Type 'n Speak's first-start dialog, y and F10
  as its keys, Alt+Shift+F letting its held keys go; Exit saving every unit used; and, in a run of its own, p-chord, l,
  then i-chord held through the restart (the cold reset's question). Its controls: `BLAZIE_GTK_BREAK=noname` (the
  keyboard area unnamed) must fail the area's check and only that one; `BLAZIE_KEYS_BREAK=1` (dots 1 and 4 swapped)
  must fail the held check. Skipped, saying why, where GTK, Xvfb, xdotool or the accessibility bus is missing; the
  libraries `blazie_emu_gtk` needs are checked (the C library, the sound library and GTK's own).

Also run by hand on a Raspberry Pi 5 (Debian 13, arm64): the input devices with a virtual keyboard (uinput) --
found, grabbed, a chord down and up, F11 opening the menu; and the same keyboard held by another program: named,
the terminal's keys used --; the menu itself in a terminal; the sound with ALSA and with the PulseAudio build,
paced by the device (about a fifth of a core); each unit started from the unpacked package.

## On a real BTSpeak

By default `blazie_emu` hands over to #4's front end there (above). With `[input] bt = native` it reads the
device's own keyboard from its keyboard server itself, which holds the keypad for itself (so the input devices cannot
read it, and letters mode is not needed there), and shows the unit's display on a braille display through BRLTTY:
[README-btspeak.md](../../platforms/btspeak/README-btspeak.md). Built on the device (Debian 12, arm64), its sound
through ALSA's default device; the braille output and the advance bars verified on a BT Braille.

Not tried on any machine: WinDisk or PCDISK on the far end of the serial port (the tests answer the unit's storage
call themselves), a real serial adapter, a real keyboard on the input devices (a virtual one was), sound actually
heard (the Pi used had no speaker: the devices took the sound at the right pace), an x86-64 build (the 0.7 work
was tested on arm64 only).
