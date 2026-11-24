# Speech Systems' Super Voice: where its disk is, and what it still needs

The Super Voice was a speech cartridge for the Tandy Color Computer, sold by Speech Systems of Batavia, Illinois,
in 1984 and 1985. Its manual calls the SSI-263 "the heart of the Super Voice". Its disk carries a text-to-speech
translator with an exception dictionary, songs, and a demonstration "showing speech obtained by controlling all
registers of SSI-263". [history.md](history.md) has the rest of what we read on it.

**It is not a voice in this project yet.** This page is here so that nobody has to hunt for the disk: searching for
its name today finds a Windows voice changer, not this.

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
Parry), the second by Del Software (written by Frank Delargy). So this project does not keep the disk in its
repository or in its releases. We are trying to reach both authors to ask.

**If you are one of them, or know how to reach them, please write to us:** open an issue on this repository.
We would like to ask who owns the software today, whether it may travel with a free program for blind users, and
whether any source or later version survives.

## Where the work stands (23 November 2026)

- **It speaks, in the workshop.** `SUPERTOK.BIN` runs unchanged on a 6809 core (MAME's, added for it) with our chip
  at $FF80 to $FF84: a text goes in, the chip's audio comes out. The code is in
  [`src/csrc/supervoice`](../src/csrc/supervoice/README.md).
- **No Color Computer is needed.** The program was run with everything outside it trapped. On the path that speaks a
  line of text (the manual's `USR` call at $5805) it calls no BASIC routine and reads none of BASIC's variables.
- **The voice layer is written** (`sv_voice.h`): rate and pitch through the program's own control codes (^S, ^IT),
  volume as gain, a capital's pitch, long text in parts, and four rules for what the program does with a screen
  reader's text, each measured on the program.
- **Still to do before it is a voice:** a place in the voice table, the voice on each platform, and a listener's
  verdict. Only its first clips have been heard so far.
- **Not known:** the cartridge's clock. 894,886 Hz (the Color Computer's own) is inferred from the program's table of
  musical notes, which is in tune at that clock and almost two semitones sharp at 1 MHz. That clock was at hand
  for a cartridge: Tandy's Color Computer Technical Reference Manual (1981), Table 5, has the E clock on pin 6 of
  the cartridge connector ("Main CPU Clock (0.89 MHz)") and Q on pin 7. That shows what the cartridge could use,
  not what its chip was wired to. A measurement or a schematic of a real cartridge would settle it; so would a
  recording of one.

When there is a voice, this page will say how to give it the file. Until its authors have answered, nothing of the
disk is in our downloads.
