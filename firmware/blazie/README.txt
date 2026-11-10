Braille Lite firmware -- not included
=====================================

The Blazie add-on runs a Braille Lite 2000's own firmware (shared with permission; this build
is the June 2003 release) in z180emu.  It is not in this repository.  The released add-on
carries it; to build the add-on yourself, put your own copies here:

  BL2ENG.BNS            the firmware image
                        sha256 a55455ebb8ccc1a9720150c53823ac484fd9aa7926b43079bf5d6ea7f2bf6002
  bl2_2003_warm.state   z180emu's snapshot of the unit booted with it (it contains the
                        firmware too)
                        sha256 fc6dffeff8c4be355455223291dabf26b0ebd567c1c02e3399f1dd1d0056e8c7

The emulator (src/apps/blazie) also runs the Braille 'n Speak 2000, English and Slovak, when
its firmware is in bns2000/ (the update disks' files, unchanged):

  bns2000/BS03ENG.BNS   English, June 24, 2003
                        sha256 9ee0af633beb744c3905e9e3a940c13873fa05faaee5ee6442f2f96cfc1d004d
  bns2000/BS2SLL.BNS    Slovak
                        sha256 8c01c59845b5609c5a1160189664b4c4e6f9fa56732503d1fd63ec026f43366f

and their factory states, made from the firmware alone by the emulator's make_state:

  make_state bns2000/BS03ENG.BNS english bns2000/bs03eng_fresh.state
                        sha256 12112afda9be28093d830db8bcd6794932a99a62d9941e50e08aa99b038bedaf
  make_state bns2000/BS2SLL.BNS english bns2000/bs2sll_fresh.state
                        sha256 6818720dc50c5f7df9a81a46f26667b74c74994de8de908442ec173a6ac27020

The emulator's game test (src/apps/blazie/test_games.c) runs Blazie's games when they are in
games/ (the June 2003 disk's files, unchanged; without them it is skipped):

  games/simon.bns       sha256 ff9230c0c1250eb44ea34f3aca9269ec7d83f6c8341a6a3b4fa6c5292a908d50
  games/hangman.bns     sha256 f42ff66964e6d64440961646ff81d6b8cd212aa75bb2e7762a943e95ec19eda9

The emulator's name brander test (src/apps/blazie/test_brand.c) runs Blazie's name brander when it
is here (the June 2003 disk's file, unchanged, documented by its BSNAME.TXT; without it the test
is skipped):

  BSNAME.BNS            sha256 a58a4aa72477fb12c813a4242a37d9ed2e7e7c6e2c52a2a2e4821baeb82667fa

The voices in German, Italian and Slovak (0.8) run these, when they are here; the add-ons, the SAPI engine and the
Linux package carry them (release assets only):

  german/Lite2000.BNS   Blazie's September 20, 2000 update, German (an anonymous donation)
                        sha256 945558fe803262d30d590c9f4dd9124a76f3a6ff9a882a788caccc024e21aabc
  italian/Lite2000.BNS  the same update, Italian
                        sha256 2af0ebd6de6b6313a5ff11066c5491cecc0326811632d5319c76d4cf8de80a8c
  bns2000/BS2SLL.BNS    the Slovak Braille 'n Speak 2000, above

and their states, made by make_state (each answers its unit's flash questions in its own language):

  make_state german/Lite2000.BNS german german/lite2000_de_fresh.state
                        sha256 db9b9b165337f9b6cb58a14e177e65c1a0de8706a8da719852fdf6681d38ce64
  make_state italian/Lite2000.BNS italian italian/lite2000_it_fresh.state
                        sha256 737b6f224c1be189b9f798e594118be6ef1654e263f11c7b115695291a20fa8e
  (the Slovak voice uses bns2000/bs2sll_fresh.state, above)

This folder's contents are ignored by git.
