Sounding Board drivers -- not included
======================================

The Sounding Board host (src/hosts/soundingboard.py) runs GW Micro's own DOS drivers for the
card, version 2.7 (1988-96, software by Douglas Geoffray).  They are not in this repository;
put your own copies here:

  SBLOAD.COM   13,044 bytes   sha256 f821c9c28180cfcecc0e5b422bb30b1b8a97d37bac7188405f066baa56cd7b72
  SB.COM        7,419 bytes   sha256 0946a2f2caa5acad54ef2113d1ebb5f40f686219eea0b3697e478dce19c9158e

Both files are packed and unpack themselves as they start; the host loads them as they are.

This folder's contents are ignored by git.
