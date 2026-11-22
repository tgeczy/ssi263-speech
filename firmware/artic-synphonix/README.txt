Artic Technologies' SynPhonix 215 -- VEST.COM goes here; it is NOT in this repository
====================================================================================

The SynPhonix research host (src/hosts/synphonix.py) runs Artic's own DOS text-to-speech for its internal speech
cards, VEST ("Visions Enhanced Speech Translator"), unchanged, on MAME's 8086 and our chip:

  VEST.COM   41,920 bytes   "VEST Ver 1.60 ENGLISH", "Special Release For Artic Visions", (C) 1984-92

That is the VEST that Business Vision 3.08 loads first (its BV.BAT runs \ARTIC\VISION\VEST, then BVISION /M3).  Put
your copy in this folder under that name.  The tests that need it (tools/check_synphonix_host.py) skip without it.

Each copy of VEST was sold with one card and holds that card's serial number, so copies differ by a few bytes and no
one sha256 names them all.  The copy the host was written on has

  sha256 e0994d955a810537214f30c773e45b6c0e6f32d5876937120b882f118c2f8039

and any VEST 1.60 should do: the host reads the serial check's own code to give the card the key that copy expects,
and refuses a file in which it cannot find that code.  Nothing else of the Business Vision disk is used.

Why it is not here: VEST.COM is Artic Technologies' software, with "All Rights Reserved" in its banner, and this
repository does not take firmware from contributors.  The maintainers alone decide what, if anything, is shipped, and
under which notice (AGENTS.md).
