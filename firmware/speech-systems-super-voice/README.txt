Speech Systems' Super Voice (1984) -- SUPERTOK.BIN goes here; it is NOT in this repository, nor in any package
=============================================================================================================

The Super Voice host (src/csrc/supervoice/sv_host.c) runs the cartridge's own text-to-speech for the Tandy Color
Computer, SUPERTOK ("V5.0A", Del Software for Speech Systems), exactly as it is on the Super Voice disk:

  SUPERTOK.BIN   10,235 bytes   sha256 4de0d8f369cf3b0cc4f0b01cfc6061f4882118098158a4bca233ce8fbe86155c

For building and testing from source, put your copy in this folder under that name.  The host accepts only that file
(by its sha256) and says so when it is missing or different; the tests that need it skip without it.

Since 0.9.5 the Super Voice is a voice on every platform ("Speech Systems Super Voice"), and every package leaves this
file out -- each build refuses it, and the disk, by name and by checksum (voices.h SSV_USER_DATA).  Each person gets
it from the disk the Color Computer Archive keeps: the NVDA add-on and the SAPI settings program download it on a
button, checked against its sha256; Linux (sd_ssi263 --import), Android and the Apple apps import it.  Only
SUPERTOK.BIN is ever kept, taken off the disk by the library's own reader (src/csrc/supervoice/sv_dsk.h).
docs/super-voice.md says where each platform keeps it.

Where the disk is: docs/super-voice.md gives the two places that keep the disk image (SUPERV.DSK, 161,280 bytes,
sha256 7dda3902a42cdc189096e023a0ad27ca6f3aba50c76a38a883892f969987b784) and its manual.  SUPERTOK.BIN is one of the
files on that disk (a LOADM file: the program at 5800h with its dictionary at 7400h).  Nothing else of the disk is
used.

Why it is not here, and why we do not mirror it: the disk lives at the Color Computer Archive, which added it on
2016/07/03 with the rest of Speech Systems' applications and their manuals (EARS, Super Talking Heads, Super Voice,
Voice Control; https://colorcomputerarchive.com/updates/2016).  Every copy this project uses is fetched from there or
its mirror, by an exact link and checksum, or brought by its own user.  A copy of our own would blur that trail and
make this project a source of the disk instead of a player that points at where it lives, so we deliberately keep
none (Tomi, 2026-11-24).  The manual agrees in its own words: the software is "for the personal use of the
purchaser", and "reproduction by any means is strictly prohibited".  Its two sections are copyrighted separately, by
Speech Systems and by Del Software.  The maintainers alone decide what, if anything, is shipped, and under which
notice (AGENTS.md).
