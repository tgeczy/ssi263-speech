# The Mockingboard's text-to-speech: which disks, and where they are kept

The two Mockingboard voices run Sweet Micro Systems' own text-to-speech for the Apple II, which this project does
not own and does not keep in its repository:

| Voice | Its firmware file | Made from |
|---|---|---|
| Mockingboard | `mockingboard-tts-1.1.bin` (11,948 bytes) | the Mockingboard Developers Toolkit (1984): text-to-speech version 1.1 of 11 March 1985 |
| Mockingboard, early | `mockingboard-tts-early.bin` (11,309 bytes) | Mockingboard disk 1: the earlier text-to-speech, with its own rules |

Where each file comes from in a download:

- **The NVDA add-on, the SAPI engine and the Linux package** carry both files.
- **The Android APK on GitHub** carries both files.
- **The app-store versions** carry neither, and the apps name no source. You import your own copy.

Each file is one version's DOS files, taken off the disk exactly as DOS stores them. Nothing else of the disk goes into
it: not Apple's DOS, and none of the disk's other programs.

## The disk images the importers recognize

The Android and Apple apps' **Import firmware…**, and `tools/mockingboard_firmware.py`, take a 140 KB Apple II disk
image in DOS order (`.dsk`, `.do`) or ProDOS order (`.po`), either alone or inside a zip. They recognize three images:

| Disk image, as the archives name it | sha256 | Gives |
|---|---|---|
| `Sweet Micro Systems Mockingboard Developers toolkit 1984.dsk` | `7b2930489cfe8952d0338d2d8751cef3bdca004075161050481da8301d0136a2` | 1.1 |
| `Mockingboard - Developer's Toolkit.dsk`, also archived as `MNBTOOLKIT for IIc.DSK` | `15cfb639ce5d9ca38bb650c8cb9662bcaf2b83282b0822fb2bfcd14463817cad` | 1.1 |
| `mockingboard1.dsk` (Mockingboard disk 1) | `0d646bd42e21521eec107001072a3a31815bd8abcd681ebf2138adbc2cea615a` | early |

They take the firmware files themselves too, either alone or in a zip, at its root or in a subfolder. A file is known
by its content, not its name. These are the two files' sha256s:

| File | sha256 |
|---|---|
| 1.1 | `88e1e90f1e76b7afa2f370db3c3bf34892c9621b5360304359242570b41bdfae` |
| early | `c7c049b1b61792719e21e461a2a8c25fc32c12882c81305814a3dc67af6e5835` |

The other Mockingboard disks are refused, with the reason given in words. Some have no text-to-speech. Others carry a
version these voices don't run:
- *Mockingboard Disk2 Snd-Speech Dev* has the early program with other rules.
- The 1982 *Speech Development System* has an unknown variant.
- The *Mockingboard D Test Disk* has the early program with the Mockingboard D's own driver.

## Where the disks are kept

They are preserved in the public Apple II archives. Each archive carries a different set:

- **The Asimov archive and its mirrors**, in the Mockingboard folder under its sound-hardware disk images. It has all
  three images above: the 1984 toolkit, the Developer's Toolkit, and disk 1. The 1984 toolkit appears to be kept only
  here.
- **ReActiveMicro's Mockingboard software downloads** have `MNBTOOLKIT for IIc.DSK`, which gives 1.1, and
  `mockingboard1.dsk`, which gives the early voice. Their `mockingboard2.dsk` holds no text-to-speech.

So either archive gives both voices.

## Their rights

Sweet Micro Systems, Inc. (Rhode Island, incorporated 1982) was revoked in 1993. No successor or claim to the software
is known. Tomi has asked its founder about the rights to it. Until he answers, the files are shipped the way the Braille
Lite's and the Speak-Out's firmware is: in the downloads, with a notice that they are not ours, are there so the card
can speak again, and will be removed if their rights holders ask. They are never in this repository.
