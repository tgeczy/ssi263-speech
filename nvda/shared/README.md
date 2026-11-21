# nvda/shared: what the add-on's drivers have in common

Each driver imports these from its own engine package, never by a shared name: the builders copy them into every
unit's `_ssi263_<unit>` folder (`../build_common.py`, `../build_blazie.py`, `../build_ssi263.py`), because one NVDA
process has one module per name and other add-ons ship modules of the same names.

| File | What it is for |
|---|---|
| `ssi263speech.py` | The units in `ssi263speech.dll` for their drivers (ctypes): the Speak-Out and Sounding Board, the two Accents, the Mockingboard, the SmarTalk. One class per unit. |
| `ssi263_rates.py` | The output sample rates the drivers offer, and why those three. |
| `ssi263_numwords.py` | Numbers as words, for the Braille Lite driver's "Custom number processing". |
| `ssi263_flow.py` | Say all's lines as one line to the unit (`play_own`: on a unit that reports its own index marks, each index at its sample). NVDA sends lines that end no sentence as one utterance with an index before each; given to a unit one by one, each was spoken as a sentence of its own ("it pauses after every line"). With "Join phrases" on, texts with only indexes between them go to the unit as one line. The Braille Lite's driver places each index by the unit's own index marks (Ctrl-F, echoed when speech reaches it); the other drivers estimate it for now, as the index's share of the line, good to a word or so. The Accents and the Sounding Board have index marks too; the module's header says which, and that they are not used yet. `../tools/say_all_flow.py` holds every driver to it. |
| `ssi263_feed.py` | An utterance's first audio, as NVDA's player gets it. A driver's first block of a word was its lead-in plus what was left of the 30 ms block the sound began in, often a sliver of 0 to 3 ms; NVDA 2025 and later trim leading silence themselves, so the stream started on the sliver and ran dry until the next block: a blip, a gap, the word (a listener's recording, 2026-11-20). `FirstFeed` stands between each driver and its player and, after a stop or an idle, holds the feeds until they carry a block of sound. A feed with a callback is never held back. Checked by `../tools/first_feed.py`, whose control takes the hold away. It also starts a stopped stream with one silent sample when an utterance has given it callbacks and no audio at all, so their indexes are called (#32; `../tools/silent_indexes.py`). |
