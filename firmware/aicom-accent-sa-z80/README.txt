Accent SA firmware (the stand-alone box, Z80) -- not included, and not a shipped voice
=====================================================================================

The stand-alone Accent's own firmware runs here on its own board (src/csrc/accentz80):
Aicom's two ROMs, read from a real Accent SA by Kevin Horton, who also traced that board.
It is not offered as a voice: on every text compared it says the same sounds at the same
pace as the Accent-PC, which is (src/csrc/accentz80/README.md has the measurements).  The
machine and its tests are kept; they run when your own copies of the ROMs are here:

  ACU5_V41.BIN   sha256 b6a5c493f5836114154c4877bb3b65959f12bf6ad9ce3fefd9d5f7d940e77bee
                 the program: "VERSION 4.1. OCT 2, 1996."  64 KB
  ACU4_V40.BIN   sha256 a9a710d62450132c7b8949f1cddf2c3f5d92a0462c2fc36f8ad63ae5ee19a990
                 the dictionary.  64 KB

Firmware 4.0 (15 February 1991) runs as well: ACU5_V40.BIN, sha256
d817e9d3a48519047df7c69c7e38ee07cdab1f62b77bb626bea1df0ad82c184c.  It differs from 4.1 by
one eight-byte patch and its date; no table differs, and both wrote the same values to
the chip in the same order on every text tried, within a third of a millisecond of each
other (src/csrc/accentz80/test_az_firmware.py checks it when both are here).

Not to be confused with ../aicom-accent-sa: that folder holds the Accent-PC card's ROMs,
which this project called "Accent SA" until November 2026.

This folder's contents are ignored by git.
