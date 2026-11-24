Artic Technologies' SynPhonix -- VEST.COM, Artic's own, with its serial set to 99999999
=======================================================================================

The Artic SynPhonix voice (src/csrc/synphonix/, and the research host src/hosts/synphonix.py) runs Artic's own DOS
text-to-speech for its internal speech cards, VEST ("Visions Enhanced Speech Translator"), on MAME's 8086 and our
chip:

  VEST.COM   41,920 bytes   "VEST Ver 1.60 ENGLISH", "Special Release For Artic Visions", (C) 1984-92
             sha256 4d74b00d8541bbe1c8ca4ec21c8bb3632f054c28d6fead97ce452679db0cdb49

That is the VEST that Business Vision 3.08 loads first (its BV.BAT runs \ARTIC\VISION\VEST, then BVISION /M3).

Each copy of VEST was sold with one card and holds that card's serial number. In this copy the serial is 99999999,
in both places it is kept (the encoded key and the banner's S/N), so it names no card and no owner. That is the only
change: 14 bytes, and VEST writes exactly the same to the chip as the copy it came from. (It cannot be all zeros:
VEST refuses such a key.) Any other copy of VEST 1.60 works here too: the host reads the serial check's own code to
give the card the key that copy expects, and refuses a file in which it cannot find that code.

TVISION.COM from the same disk (Business Vision for the TransPort) is not kept here. It may go beside VEST.COM for
the research script tools/artic/tvision_serial.py only.

Its notice -- this file is NOT under the repository's MIT licence:

  VEST.COM is Copyright 1984, 1985, 1988, 1989, 1990, 1992, Artic Technologies, All Rights Reserved.

No licence to share it has been granted. Artic sells nothing today and seems to have closed long ago, and no one we
know of has taken its programs on (docs/history.md, "Who speaks for Artic"). We include it so that Artic's own voice
can be heard again, the way the SynPhonix and the TransPort spoke to their users. Calling it abandoned describes the
preservation problem; it is not a licence.

If you hold rights to VEST -- one of Artic's founders, or whoever took on its work -- we would be glad to hear from
you, whatever you would like done: open an issue on this repository, or write to the maintainer at the e-mail address
on his GitHub profile. If you want it removed, it will be removed.

The NVDA add-on carries VEST.COM with this notice (nvda/build_ssi263.py), as will the SAPI voices, the Linux package
and the GitHub Android APK. The Apple apps never do: they stay free of firmware, and VEST.COM is imported there like
the others.

Not to be confused with the Virginia Embedded Systems Toolkit (VEST, University of Virginia, about 2001), a design
tool for embedded software with nothing to do with speech.
