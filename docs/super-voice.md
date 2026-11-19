# Speech Systems' Super Voice: where its disk is, and what it still needs

The Super Voice was a speech cartridge for the Tandy Color Computer, sold by Speech Systems of Batavia, Illinois,
in 1984 and 1985. Its manual calls the SSI-263 "the heart of the Super Voice". Its disk carries a text-to-speech
translator with an exception dictionary, songs, and a demonstration "showing speech obtained by controlling all
registers of SSI-263". [history.md](history.md) has the rest of what we read on it.

**It is not a voice in this project yet.** This page is here so that nobody has to hunt for the disk: searching for
its name today finds a Windows voice changer, not this.

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

## What it needs before it can speak here

- **A 6809 processor.** The text-to-speech is 6809 code, and this project has no 6809 core yet.
- **A host shaped like a Color Computer:** the programs are loaded at fixed addresses and called from BASIC, and they
  write the chip at $FF80 to $FF84.

Nothing on the disk has been run here; it has only been read.

When there is a voice, this page will say how to give it the disk. The plan: a button that fetches the file above and
checks its sha256 where a program can do that, and by the usual import on a phone.
