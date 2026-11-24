# tools/artic: exploring Artic's own programs

Research scripts for Artic Technologies' SynPhonix and TransPort, run on the SynPhonix research host's machinery
(`src/hosts/synphonix.py`, MAME's 8086 through `libpc86`). No test rests on them; `tools/check_synphonix_host.py`
is the host's test. What they found is in [`docs/front-ends.md`](../../docs/front-ends.md), "Artic SynPhonix 215".
Both look in `firmware/artic-synphonix/`: `VEST.COM` is kept there under its notice; `TVISION.COM` is not in the
repository and goes there as a local copy.

| File | What it is for |
|---|---|
| `vest_probe.py` | VEST.COM's interface by hand: texts with a chosen option byte (AH) and offsets, the raw-frame calls (AL=4, AL=5), and a small exception dictionary in VEST's format |
| `tvision_serial.py` | TVISION.COM (Business Vision for the TransPort) with a UART on COM1 and hand-written replies, printing every byte it sends. It skips TVISION's serial comparison in memory, so no serial is ever needed or answered |

`tvision_serial.py`'s replies are written for TVISION, not copied from a unit. A Mini TransPort answers `^E~?` with
"TP" and eight digits and no carriage return, `^EQ` with the marker letter alone, and does not answer `^E%` at all:
its pedals are read with Ctrl-A P (ACK "PFF+" CR). The manuals' command list and what a Mini TransPort did with each
command are in `docs/front-ends.md`.

## Settled, and from where

- `^EW`, `^EIZ`, `^EQ`, `^E$` and `^EN`: wait for speech, set index marker Z, query it, reset, number processor off
  or on. The TransPort's manual (`TRANSPRT.DOC`, Appendix C) and the Mini TransPort's (`MINIPORT.DOC`, Appendix B),
  and a Mini TransPort over its serial port on 22 November 2026.
- Ctrl-A Q and `^E~?` are in no manual. A Mini TransPort answers Ctrl-A Q with ACK "Q+" CR and then ACK "PFF+" CR,
  the same reading as Ctrl-A P, and `^E~?` with "TP" and eight digits.

## Open, and how to settle it

- The order of `^EZ`'s three digits (rate, pitch, volume): TVISION run with a configuration file, or `^EZ` with
  digits sent to a real unit. `^EZ` alone makes a Mini TransPort speak at about 40% of its speed until `^E$`.
- What `^E~?`'s eight digits and Ctrl-A Q's "Q+" mean, and whether a TransPort 615 answers `^E%`.
- The SynPhonix's chip clock: a real unit's recording of a sentence against the register values VEST gives for it.
  By ear a Mini TransPort speaks at the host's speed at 1 MHz, with a lower pitch after `^E$`.
- The second dictionary (AH bit 4, the segment in BX): its entries are not decoded (text module 1806).
