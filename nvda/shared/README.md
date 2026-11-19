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
