# SSI-263P phoneme parameter ROM — die-shot extraction

**Feature**: `024-mockingboard-speech` | **Issue**: #123 | research.md D11 route C

This directory holds what we believe is the first read of the SSI-263's
phoneme parameter ROM. The chip's ROM was never published; MAME and AppleWin
substitute SC-01A data, which has a different phoneme order. Everything here
was extracted from visual6502's published die photograph.

## Provenance

- Source image: `SSI_263P_20x_1a_7000w.jpg` (7000 x 5803), from
  <http://www.visual6502.org/images/pages/Silicon_Systems_SSI_263P_die_shots.html>
- ROM region: x 2350-3645, y 3100-3745 of that image
  (`rom-region-source.jpg` is that crop, unmodified).
- Extraction was a joint human/agent effort against the published 7000-wide
  image only; the full 17,265 x 14,313 master (requested in
  `../visual6502-request-draft.md`) would allow independent confirmation.

## Physical organization

- **64 columns**, one per phoneme. The block below the array is the column
  address decoder: six wire pairs carrying the binary patterns of period
  2, 4, 8, 16, 32, 64 (LSB nearest the array). Its phases give the
  column-to-code mapping directly.
- **Column mapping is mirrored**: image column `c` (left to right) holds
  phoneme code `63 - c`.
- **43 physical row positions** at ~13.4 px pitch, organized as 14 groups of
  (2 data rows + 1 always-empty spacer row), plus one final unpaired data
  row. Data rows are labeled `b00..b27` top to bottom, plus `PAR` (the lone
  final row; the label predates knowing what it is - it is *not* parity).
- A **set bit is a stadium-shaped contact oval** in the light metal column;
  two vertically adjacent set bits fuse into one double-height oval. An
  empty cell is plain metal. 749 of 1856 cells are set (40%).

## Extraction method and validation

1. Column centers tracked per row (centroid following, no straight-line
   assumption); row lattice pinned by autocorrelation (period 40.239 px per
   3-row group).
2. Per-cell classification by normalized-patch nearest-centroid against
   set/empty templates learned from 45 hand-labeled cells (columns 0 and 32),
   with +/-shift alignment search.
3. Validation: 45/45 leave-one-out after label arbitration (every
   human/classifier disagreement was re-examined at 10x zoom; the classifier
   won all nine); 0 false positives on 112 spacer-row probe cells; a single
   low-confidence cell (C09/b01) was resolved by human inspection; column
   C06 was fully hand-audited (29/29 agree); further user spot checks agree.

## Identified semantics

| Bits | Meaning | Evidence |
|---|---|---|
| b00 | Closure flag (clear = closure onset) | Clear exactly for B, D, P, T, K, HVC, HFC |
| b01-b02 | Manner class bits | Vowels `##`, stops/holds patterns differ by class |
| b03 | Fricative flag (oval = non-fricative) | 63/64 vs datasheet, sole exception PA |
| b04 | Voiced flag (oval = unvoiced) | 63/64 vs datasheet, sole exception PA |
| b09, b10 | Spectral (formant) bits | Stable within every duration-variant family, flip across vowels (b10: 0% within, 56% across) |
| b12, b15, b16, b18, b21, b22, b25, b27, PAR | Duration/amplitude cluster | These are what distinguish E/AY, UH/UH1/UH2/UH3, R/R1/R2 |
| b08, b14 | Always zero (all 64 phonemes) | Unused field MSBs or field separators |

Cross-validation from structure alone:

- E ($01) and AY ($05) — identical formants in the phonetics literature —
  are **bit-for-bit identical in b00-b27**, differing only in PAR.
  Likewise AI ($09) and :A ($3A). PAR is therefore non-acoustic;
  the datasheet's E-vs-AY distinction is duration.
- HV/HVC and HF/HFC differ only in the closure-region bits.
- PAR is **not column parity**: even parity holds in only 35/64 columns,
  including a fully hand-audited counterexample (C06), and no row subset
  computes it (best of 406 contiguous ranges: 43/64).

## DECODED (2026-08-26): the full field structure

The key came from MAME's SC-01 work (`votrax.cpp`, from the 2007 sc01a.bin
decap): Gagnon's designs store parameters **significance-interleaved** —
one bit-plane of every parameter, then the next plane. Applying that scheme
at stride 6 over b05-b27+PAR decodes the whole word
(`rom-decoded-params.csv`):

| Field | Rows (MSB..LSB, oval=1) | Identity | Evidence |
|---|---|---|---|
| flags | b00-b04 | closure, class x2, ~fricative, ~voiced | see table above |
| FA | b05,b11,b17,b23 | Fricative/noise amplitude | 0 for ALL vowels+sonorants; 15 for S/P/T, graded down SCH/F/TH |
| VA | b06,b12,b18,b24 | Voice amplitude | 0 for all unvoiced; 8-12 vowels; low for voiced fricatives |
| F3 | b07,b13,b19,b25 | F3 filter code | rho 0.85 vs literature; R=1/ER=3 = the retroflex low-F3 signature |
| NASAL | b08,b14,b20,b26 | Nasal coupling (2 active bits) | =3 for exactly M, N, NG, HN; 0 elsewhere (hence the all-zero rows) |
| F2 | b09,b15,b21,b27 | F2 filter code | **rho 0.94** vs literature; E=14..AH=3..U=1..W=0 monotone front-to-back |
| F1 | b10,b16,b22,PAR | F1 filter code | rho 0.89; E=2..AE=13..AH=15..U=3 = the vowel-triangle trajectory |

"PAR" was F1's least-significant bit all along — which is why E/AY and
AI/:A differ only there (adjacent F1 codes, identical in the literature),
and why it never computed as parity.

PA ($00) decodes to FA=0, VA=0 with mid-tract filter codes: silence with a
parked vocal tract — the hardware behavior behind "pauses hold the tract."

**Open**: the code-to-Hz mapping per filter (the switched-capacitor DAC
weights). The SC-01 precedent (`votrax.cpp` bits_to_caps) says codes index
near-binary-weighted capacitor sums; calibrating our three filter code
tables to frequencies needs either the die-level capacitor geometry (the
full-res master) or empirical fitting against reference audio/formant
targets. Amplitude scaling (FA/VA codes to gain) likewise.

What the field investigation established (2026-08-26):

- The datasheet block diagram is the architecture key: the tract is **five
  cascaded programmable low-pass filter sections** (not three resonators),
  fed by a "Phoneme Characteristics ROM", with glottal vs noise excitation
  selected per phoneme — our b04/b03 flags are exactly that selector.
  Arithmetic fits six 4-bit values (five filters + amplitude) in the 24
  non-flag bits, but the naive nibble split does not correlate best.
- The physical row-pair interleave is real: reading every-other data row as
  one stream lifts F2 rank-correlation from 0.72 to **0.88** (rows
  b09,b11..b21 MSB-first) and F3 to 0.87 — the odd/even row streams carry
  distinct parameter data, echoing the SC-01 patent's multiplexed nibble
  ROM output.
- Ruled out: plain contiguous binary fields; pure thermometer (unary
  capacitor-count) coding; six aligned nibbles at the obvious boundaries;
  PAR as column parity. The bit-to-capacitor assignment is scrambled
  beyond what correlation against an approximate reference table resolves.

Patent status: no SC-02/SSI-263-specific patent found. US4433210 (SC-01,
Federal Screw Works) documents the predecessor's 12-parameter mostly-4-bit
word and its multiplexed nibble ROM bus; US4829573 (Votrax International,
filed 1986, Gagnon/Houck) is a later K-parameter design whose **microfiche
appendix contains a complete hex parameter table for 63 phonemes** —
obtainable from USPTO as a file-wrapper copy, and potentially a Rosetta
stone for Votrax parameter conventions even across generations.

Remaining decode routes, strongest first: (1) trace the ROM column outputs
to the filter capacitor banks on the full-resolution die master (the
drafted visual6502 request); (2) obtain the US4829573 microfiche appendix;
(3) forward-model five cascaded switched-cap low-pass sections and search
bit-assignments that reproduce expected phoneme spectra.

## Files

| File | Contents |
|---|---|
| `rom-region-source.jpg` | Unmodified crop of the ROM region from the visual6502 image |
| `rom-decoded-plate.png` | **The final plate**: every bit boxed by value, rows labeled with decoded field + bit significance, columns labeled by phoneme |
| `decoded-data.md` | The decoded data write-up: field map, cross-checks, and the full 64-phoneme table |
| `rom-region-annotated.png` | Every cell boxed green/red with the b/PAR/column numbering |
| `rom-bits.csv` | Machine-readable matrix, phoneme-ordered (`0x00`-`0x3F`), bit = oval present |
| `rom-bits-by-phoneme.txt` | Human-readable dump in phoneme order |
| `rom-bits-by-column.txt` | Human-readable dump in image-column order |

Bit convention in all files: `1`/`#` = contact oval present. Note the flag
rows read inverted (oval = unvoiced / non-fricative); polarity of the
numeric fields is not yet established.
