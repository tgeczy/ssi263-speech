# The Blazie emulator on a BT Speak and a BT Braille

The Braille Lite 2000, the Braille 'n Speak 2000 and the Type 'n Speak, running their own firmware, on Blazie
Technologies' notetakers: the BT Speak's own braille keys are the Braille Lite's keys, and on a BT Braille the
Braille Lite's 18-cell display appears on the BT Braille's display, panned with the BT Braille's own panning keys
as the Braille Lite's advance bars.

It is the Linux emulator (`blazie_emu`, [README-linux.md](../../apps/blazie/README-linux.md)) itself, with no Python
needed on the device: on a BT Speak or BT Braille it reads the keyboard from the device's keyboard server and shows
the unit's display through BRLTTY, so any braille display BRLTTY drives works. The Braille Lite's two advance bars
work everywhere the emulator runs. This is `bt = native` under `[input]` in the settings: by default `blazie_emu`
hands over to #4's front end (`blazie_emu_bt`, [README.md](README.md)), with the device's own Blazie Mode dialogs, and
uses this route itself only when that front end is not installed beside it.

## Install from the release

Download the Linux arm64 package (`ssi263-speech-<version>-linux-aarch64.tar.gz`) to the BT Speak, then in a
terminal:

    tar -xzf ssi263-speech-*-linux-aarch64.tar.gz
    cd ssi263-speech-*-linux-aarch64
    ./install-btspeak.sh --menu

That is all: "Braille Lite 2000" is now in the User Menu (and the Type 'n Speak, when its firmware is packaged).
The package carries the firmware. `./uninstall-btspeak.sh` removes it again. The package must have been built on
Debian 12 or older (a BT Speak's system: glibc 2.36); one built on Debian 13 will not start.

## The .deb (the easiest way)

Install it with apt:

    sudo apt install ./blazie-emu-btspeak_<version>_arm64.deb

It puts the emulator in `/usr/lib/blazie-emu-btspeak`, fetches the firmware from this project's own release download
(checked against its published SHA-256: the firmware is never inside the .deb), and adds Braille Lite 2000,
Braille 'n Speak 2000 and Type 'n Speak to the User Menu of every BT Speak user on the device. Without a network at
the time, it installs all the same and the menu entry fetches the firmware on first start.
`sudo apt remove blazie-emu-btspeak` takes the program, the fetched firmware and those three menu lines away; the
rest of the User Menu is left exactly as it was, and the units' saved memory stays.

Built with `src/platforms/btspeak/deb/build_deb.sh` after `./build_linux.sh`, on Debian 12 or older. The firmware
comes from the release pinned in `deb/fetch-firmware` (v0.7.0's `blazie-emu-0.7.0-linux-aarch64.tar.gz`, by its
SHA-256); should that download be gone or changed, from the latest release's `blazie-emu-<version>-linux-aarch64.tar.gz`,
checked against that release's own `SHA256SUMS.txt`. So a new release needs no new package for the firmware; moving
the pin to it is optional.
`tools/btspeak_deb_test.sh` checks the package's scripts against a scratch root (`tools/linux_tests.sh` runs it).

## Testing a build before it is released

A tester's package (`tools/package_btspeak.sh`: `blazie-emu-btspeak-<version>-linux-aarch64.tar.gz`) has the new
program but no firmware. Download it and the project's own `blazie-emu-<version>-linux-aarch64.tar.gz` from its
latest release (that one carries the firmware), then:

    tar -xzf blazie-emu-btspeak-*-linux-aarch64.tar.gz
    cd blazie-emu-btspeak-*-linux-aarch64
    ./install-btspeak.sh --menu ~/Downloads/blazie-emu-0.7.0-linux-aarch64.tar.gz

(the second file's name and folder as you saved it). The firmware is taken from it as it is; nothing else of it is
installed.

## Build and install from the source

On the BT Speak or BT Braille itself (both run Debian 12, arm64):

    sudo apt install build-essential libasound2-dev
    ./build_linux.sh
    src/platforms/btspeak/install.sh --menu

The firmware is not in the repository: put it in `firmware/blazie/` first (its README.txt names the files), or give
the folder: `src/platforms/btspeak/install.sh --menu /path/to/firmware`.

`install.sh` puts `blazie_emu` and `blazie_files` in `~/.local/bin` and the firmware in `~/.local/share/ssi263-speech`
(no root needed; `PREFIX=/usr/local sudo -E ...` for every user). `--menu` adds a line per unit to the BT Speak's
User Menu (`~/BTSpeak/user.menu`): Braille Lite 2000, and the Braille 'n Speak 2000 and Type 'n Speak when their
firmware is there. Choose one there to start it. `src/platforms/btspeak/uninstall.sh` takes it all away again (the
units' saved memory, in `~/.config/ssi263-speech/blazie-emu`, is kept).

## Keys

While the emulator runs, the keyboard is the unit's, not BRLTTY's (the program asks the keyboard server for it
alone). The keys are the same on both routes, with two differences in #4's front end (the default): dots 7 and 8
alone are not bars there, and its menu is the device's own dialogs rather than lines BRLTTY reads.

| BT Speak / BT Braille | Braille Lite |
| --- | --- |
| dots 1-6, space | dots 1-6, the space bar: chords exactly as on the unit, keys held seen as held (p-chord, l, then hold i-chord: the cold reset) |
| dot 7 alone | the back bar |
| dot 8 alone | the advance bar |
| BT Braille: L2 or R2 | the back bar |
| BT Braille: L3 or R3 | the advance bar (and any other keys set to pan the display in Braille Settings) |
| M-chord with dot 7: space, dots 1 3 4 7 | this program's menu |
| Z-chord with dot 7: space, dots 1 3 5 6 7 | save the unit and leave |

The two gestures are #4's, acted on when every key is up. Any other chord with dot 7 or 8 is the program's too, and
none of it reaches the unit (the Braille Lite has no dots 7 and 8). A bar is down for as long as you hold it; a
quick tap is held long enough for the unit to see it. A chord typed while a bar is held is the unit's bar-and-chord
command. Dot 7 or 8 waits 80 ms for a chord's other keys before it acts as a bar, so a gesture never moves the
display (`[btspeak] gesture_ms`).

**The menu** gives the keyboard back to BRLTTY while it is open: type a number and Enter (dot 8), as anywhere on
the device; Enter alone goes back to the unit, which takes the keyboard again. 0 switches the unit off (its memory
is saved) and ends the program, as Z-chord with dot 7 does. The menu's other choices are the Linux emulator's
(README-linux.md).

The Type 'n Speak needs a QWERTY keyboard: a USB one plugged into the device.

## The braille display

On a BT Braille the Braille Lite's 18 cells are shown at the left of the display, the rest blank, as the unit shows
them -- its own dots, all eight, from its own firmware. While the unit runs, the display is the program's; in the
menu, and for the Braille 'n Speak and the Type 'n Speak (which have no display), it is BRLTTY's.

Any braille display BRLTTY drives works the same way (one plugged into a BT Speak): there, whichever keys pan
BRLTTY's display (its FWINLT and FWINRT commands) are the back and advance bars, tapped.

## Settings

`~/.config/ssi263-speech/blazie-emu/blazie_emu.ini`, section `[btspeak]`:

| | |
| --- | --- |
| `keyboard = auto` | the device's keyboard when there is a keyboard server; `off`: the terminal's characters, as before |
| `gesture_ms = 80` | how long dot 7 or 8 waits for a chord's other keys |
| `display = auto` | the unit's display on BRLTTY's display when it has one; `off`: never |

and in `[keys]`, `back` and `advance` name the keys that are the bars (`brl_dot7`, `brl_dot8` on these devices).
`[input] bt`: `auto` (hands over to #4's `blazie_emu_bt`; without it, as `native`), `native` (this program uses the
device itself), `off` (the terminal only).

## The advance bars

The Braille Lite 2000 has two advance bars, and its firmware does not read them where the emulator used to put them
(the chord port's bit 7, which it masks off). They are bits 6 (advance) and 7 (back) of the 8255's port B, low while
pressed, polled ten times a second; the firmware's own setting can swap them. The display is clocked out through
the same 8255's port C. Leo (#4) and this work found both independently; the board code is #4's
(`src/csrc/blazie/bl_display.h`, `bl_board.c`), and `emu_unit.c` keeps each bar down long enough for the firmware
to see a tap, and gives a chord typed with a bar once the firmware has the bar.

## What has been tried

On a BT Speak (Compute Module 4, Debian 12): the build; the device's keyboard, its chords and bars, in the running
emulator; the sound through ALSA's default device (the automatic sound buffer settled at 100 ms). On a BT Braille:
the Braille Lite's display on the device's display, and the advance bars panning it as designed. The whole program
with a keyboard server of the tests' own (`test_emu_linux.py --only btspeak`): every key consumed, the chords and
bars to the unit, both gestures, the keyboard given back and taken again.
