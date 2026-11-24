# Speech Systems' Super Voice: where its disk is, and how each platform gets it

The Super Voice was a speech cartridge for the Tandy Color Computer, sold by Speech Systems of Batavia, Illinois,
in 1984 and 1985. Its manual calls the SSI-263 "the heart of the Super Voice". Its disk carries a text-to-speech
translator with an exception dictionary, songs, and a demonstration "showing speech obtained by controlling all
registers of SSI-263". [history.md](history.md) has the rest of what we read on it.

**Since 0.9.5 it is a voice on every platform, named "Speech Systems Super Voice"** (the maker first, as the Artic
SynPhonix is). No package of this project carries its program: each person gets it from the disk the Color Computer
Archive keeps (below). This page is also here so that nobody has to hunt for that disk: searching for its name today
finds a Windows voice changer, not this.

## Getting it, platform by platform

Only one file of the disk is ever kept: the program `SUPERTOK.BIN`, taken off the disk by the library's own reader
([`src/csrc/supervoice/sv_dsk.h`](../src/csrc/supervoice/sv_dsk.h)) and accepted only when it is the known program, by
its sha256. You can also give it the program itself, if you have taken it off the disk already.

| Platform | How | Where it is kept |
|---|---|---|
| NVDA | NVDA's Tools menu, **SSI-263 speech data**: **Download the Super Voice** (from the Color Computer Archive, or apps.irata.online if the first fails; each checked against its sha256), or **Import a file** (the zip, `SUPERV.DSK` or `SUPERTOK.BIN`) | NVDA's configuration folder: `%APPDATA%\nvda\ssi263-speech\firmware\speech-systems-super-voice\` for an installed NVDA, so it survives add-on updates, goes with a portable copy, and reaches the sign-in screen with your settings |
| SAPI | the settings program, **Speech data**: **Download the Super Voice** or **Import a file**, then **Data location** and **Move voices for all users** as you need them (Panthera's data-folder system) | the data folder the settings program shows: NVDA's own folder above when you have one, else `%APPDATA%\ssi263-speech\firmware`, or `%ProgramData%\ssi263-speech\firmware` once moved for every account; the voice's token records it |
| Linux | `sd_ssi263 --import SUPERV.DSK` (or `SUPERTOK.BIN`; unzip the archive's zip first) | `~/.local/share/ssi263-speech/speech-systems-super-voice/` (`$XDG_DATA_HOME`), or the package's data folder if you put it there |
| Android | Settings, **Import firmware**: the zip, `SUPERV.DSK` or `SUPERTOK.BIN` | the app's own storage |
| iPhone, iPad, Mac | import, as every unit's firmware there | the app's own storage |

The download happens only when you press the button. Nothing about you is sent with the request.

**Other things share the name.** This is not SuperVoice, the fax and voicemail program that came with modems in the
1990s, and not any of the Windows voice changers called Supervoice. If a file is not one of the two below, by its
checksum, it is not this disk.

## The disk, exactly

The Color Computer Archive keeps it. Both copies below hold the same disk image, byte for byte (checked 19 November
2026):

| What | Where | Size | sha256 |
|---|---|---|---|
| `Super Voice (Speech Systems).zip`, with `Super Voice (Speech Systems)/SUPERV.DSK` inside | [colorcomputerarchive.com](https://colorcomputerarchive.com/repo/Disks/Applications/Super%20Voice%20%28Speech%20Systems%29.zip) | 28,456 bytes | `46a37e083a002dfda269dc69be4f06f3b87ef4272a444a78c670a98e888b81fa` |
| `SUPERV.DSK`, the disk image alone | [apps.irata.online](https://apps.irata.online/COCO/Applications/Super%20Voice%20%28Speech%20Systems%29/SUPERV.DSK) | 161,280 bytes | `7dda3902a42cdc189096e023a0ad27ca6f3aba50c76a38a883892f969987b784` |

The manual, 25 pages:
[Super Voice (Speech Systems).pdf](https://colorcomputerarchive.com/repo/Documents/Manuals/Applications/Super%20Voice%20%28Speech%20Systems%29.pdf),
also at the Color Computer Archive.

The programs that do the talking are `TRNSLATE.BIN`, `SUPERTOK.BIN` and `HEADS.BIN`, with the dictionary
`DICT32K.BIN`. They are by Del Software of Westford, Massachusetts, and carry dates from April 1984 to February
1985.

## Why it is not in our downloads

The manual's notice says the software is "for the personal use of the purchaser" and that "reproduction by any means
is strictly prohibited". Its two sections are copyrighted separately: the first by Speech Systems (written by Rich
Parry), the second by Del Software (written by Frank Delargy). So this project does not keep the disk, or any file of
it, in its repository or in its releases: the Color Computer Archive has kept the disk since 2016, and the voice
fetches it from there, or takes your own copy (Tomi, 2026-11-23). Every package's build refuses the program and the
disk by name and by checksum. We are trying to reach both authors to ask.

**If you are one of them, or know how to reach them, please write to us:** open an issue on this repository.
We would like to ask who owns the software today, whether it may travel with a free program for blind users, and
whether any source or later version survives.

## Where the work stands (23 November 2026)

- **It is a voice.** In the voice table as `supervoice:supertok`, "Speech Systems Super Voice", after the Artic
  SynPhonix; on NVDA, SAPI, speech-dispatcher and Android, and handed to the Apple apps' port. Its numbers are the
  program's own by default: it reads money, decimals and numbers up to the trillions itself (it says "1st" as "one S
  T"; the custom number processing, off by default here, reads ordinals).
- **It speaks.** `SUPERTOK.BIN` runs unchanged on a 6809 core (MAME's, added for it) with our chip
  at $FF80 to $FF84: a text goes in, the chip's audio comes out. The code is in
  [`src/csrc/supervoice`](../src/csrc/supervoice/README.md).
- **No Color Computer is needed.** The program was run with everything outside it trapped. On the path that speaks a
  line of text (the manual's `USR` call at $5805) it calls no BASIC routine and reads none of BASIC's variables.
- **The voice layer is written** (`sv_voice.h`): rate and pitch through the program's own control codes (^S, ^IT),
  volume as gain, a capital's pitch, long text in parts, and four rules for what the program does with a screen
  reader's text, each measured on the program.
- **Still to do:** a listener's verdict on it as a voice, through a screen reader. Only its first clips have been
  heard so far.
- **Not known:** the cartridge's clock. 894,886 Hz (the Color Computer's own) is inferred from the program's table of
  musical notes, which is in tune at that clock and almost two semitones sharp at 1 MHz. That clock was at hand
  for a cartridge: Tandy's Color Computer Technical Reference Manual (1981), Table 5, has the E clock on pin 6 of
  the cartridge connector ("Main CPU Clock (0.89 MHz)") and Q on pin 7. That shows what the cartridge could use,
  not what its chip was wired to. A measurement or a schematic of a real cartridge would settle it; so would a
  recording of one.

Until its authors have answered, nothing of the disk is in our downloads: each person gets it from the archive.
