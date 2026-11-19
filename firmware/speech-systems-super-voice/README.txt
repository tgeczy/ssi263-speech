Speech Systems' Super Voice (1984) -- SUPERTOK.BIN goes here; it is NOT in this repository
==========================================================================================

The Super Voice host (src/csrc/supervoice/sv_host.c) runs the cartridge's own text-to-speech for the Tandy Color
Computer, SUPERTOK ("V5.0A", Del Software for Speech Systems), exactly as it is on the Super Voice disk:

  SUPERTOK.BIN   10,235 bytes   sha256 4de0d8f369cf3b0cc4f0b01cfc6061f4882118098158a4bca233ce8fbe86155c

Put your copy in this folder under that name.  The host accepts only that file (by its sha256) and says so when it is
missing or different; the tests that need it skip without it.

Where the disk is: docs/super-voice.md gives the two places that keep the disk image (SUPERV.DSK, 161,280 bytes,
sha256 7dda3902a42cdc189096e023a0ad27ca6f3aba50c76a38a883892f969987b784) and its manual.  SUPERTOK.BIN is one of the
files on that disk (a LOADM file: the program at 5800h with its dictionary at 7400h).  Nothing else of the disk is
used.

Why it is not here: the manual's notice says the software is "for the personal use of the purchaser" and that
"reproduction by any means is strictly prohibited".  Its two sections are copyrighted separately, by Speech Systems
and by Del Software.  The maintainers alone decide what, if anything, is shipped, and under which notice (AGENTS.md).
