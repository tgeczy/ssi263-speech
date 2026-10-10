# The SSI-263 voices for Linux

The NVDA add-ons' voices, for speech-dispatcher: each a real speech device emulated, its own firmware running on an
emulated processor (MAME's cores) and driving a register-level model of the Silicon Systems SSI-263 speech chip. The
rules, number reading and inflection are the firmware's own, live. Nothing is recorded or concatenated.

| Voice | Language | The device |
|---|---|---|
| Braille Lite 2000 | English | Blazie's notetaker in speech-box mode, its June 2003 firmware on a Z180 |
| Braille Lite 2000 (español) | Spanish | the same, its Spanish firmware (when included) |
| Accent SA | English | Aicom's serial speech box, its own 8085 firmware and dictionary ROMs |
| Accent-mini | English | Aicom's Accent-mini, its DOS driver on an emulated PC (when the package has it) |
| Speak-Out | English | GW Micro's 1995 talking box, its V40 firmware (when the package has it) |
| Mockingboard | English | Sweet Micro Systems' Apple II speech card, its text-to-speech version 1.1 on an emulated 6502 (when the package has it) |
| Mockingboard, early | English | the same card, the earlier text-to-speech of Mockingboard disk 1 with its own rules (when the package has it) |

This is a speech-dispatcher module, so Orca and anything else that speaks through speech-dispatcher can use it.
Each voice is the same voice as in its NVDA add-on.

## Install

    tar xzf ssi263-speech-*-linux-*.tar.gz
    cd ssi263-speech-*-linux-*
    sudo ./install.sh
    killall speech-dispatcher
    spd-say -o ssi263 "Hello from the Braille Lite"
    spd-say -o ssi263 -y "Accent SA" "Hello from the Accent"

`install.sh` adds the voices and leaves your default synthesizer alone: speech-dispatcher finds the module by itself, so `speechd.conf` isn't changed (an `AddModule` line there would turn that off and leave this the only synthesizer; thanks, Garrett). Only if your `speechd.conf` already lists its modules with `AddModule` lines, as Raspberry Pi OS does, is this one added to the list. Installing over 0.7.6 or earlier removes the line those added. `sudo ./install.sh --default` also makes it
the default. In Orca: Preferences, Speech, Speech synthesizer: ssi263, then the voice (`spd-say -o ssi263 -L` lists them).
`sudo ./uninstall.sh` removes everything it added.

Settings are in `ssi263.conf` beside speech-dispatcher's other module settings (the installer prints where), each
explained in the file: the sample rate (11, 22 or 44 kHz) for every voice; the Braille Lite's voice inflection, hiss
or whine, tone, "short pauses" line packing, numbers read as words (`SSI263BrailleLiteNumbers`; on by default, as in
the NVDA add-on, English and Spain's Spanish) and the experimental "run the unit ahead" (`SSI263RunAhead 1`; off by
default, as in the NVDA add-on); the Accents' inflection, number reading and (the Accent-mini's) voice; the
Speak-Out's tone, "join phrases" and "shorten pauses". For your own settings, without root and kept when you reinstall, copy any of
those lines into `~/.config/ssi263-speech/sd_ssi263.conf`: they win over the module's file. After a change,
`killall speech-dispatcher` (Orca reconnects by itself). Rate, pitch and volume come from Orca or spd-say, mapped
onto each device's own: the middle is its factory rate and pitch.

Builds: x86_64 and aarch64 (a Raspberry Pi 4 or 5 is fine: the unit runs several times faster than real time on a
Pi 5). No Python, no other packages: one program.

## The Blazie emulator

`bin/blazie_emu` is the whole unit in a terminal -- a Braille Lite 2000 or a Type 'n Speak running its own firmware,
booting to its own main menu, its files and settings kept between runs -- for a Raspberry Pi's console, a desktop's
terminal or a BTSpeak. `./bin/blazie_emu` from this folder (`install.sh` also puts it in `/usr/local/bin`); F11 is
its menu. `README-blazie-emu.md` has the keys, the sound, the serial port and the BTSpeak notes.

## The firmware, and the licences

This package carries the Braille Lite's own firmware (and the Type 'n Speak's, for the emulator), shared with
permission. It is not ours; it is here so the
unit can speak again, and it will be removed if its rights holders ask.

It carries Aicom's software for the Accents: the Accent SA's firmware and dictionary ROMs, and the Accent-mini's
driver when the package has that voice. They are Aicom Corporation's work, not ours, and not covered by the licences
below; `licenses/Aicom-notice.txt` says where they come from and why they are here.

When the package has the Speak-Out, it carries the Speak-Out's own firmware (hardware Daniel Weirich, software Douglas
Geoffray, GW Micro). It is not ours; it is here so the box can speak again, and it will be removed if its rights
holders ask (`licenses/Speak-Out-firmware-notice.txt`).

When the package has the Mockingboard voices, it carries Sweet Micro Systems' text-to-speech for the card (version 1.1
from the Mockingboard Developers Toolkit disk, and the earlier one from Mockingboard disk 1, whichever ship). They are
not ours; they are here so the card can speak again, and they will be removed if their rights holders ask
(`licenses/Mockingboard-firmware-notice.txt`).

The program and library are MIT (`LICENSE`; the chip model draws on Casso's, also MIT:
`licenses/Casso-MIT.txt`), except the CPU cores, which are MAME's and keep their BSD-3-Clause licences
(`licenses/MAME-Z180-core-BSD-3-Clause.txt`, `licenses/MAME-8085-core-BSD-3-Clause.txt`, and the 8086's and V40's
with those voices). The source and how to build it:
https://github.com/tgeczy/ssi263-speech
