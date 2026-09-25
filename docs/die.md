# Reading the chip off its die

The SSI-263 keeps what each phoneme sounds like in a small ROM on the chip: 64 phonemes,
29 bits each. The chip gives no way to read it over its bus (a read returns only the
request bit), so the only way in is a photograph of the silicon. This page records how
that ROM was read, how the reading was checked, what is settled, and what is not.

## The photograph

Everything here comes from one picture:
[Visual6502.org's SSI-263P die shots](http://www.visual6502.org/images/pages/Silicon_Systems_SSI_263P_die_shots.html).

- **Who made it:** an anonymous donor sent Visual6502 two SSI-263P chips. Greg James
  photographed the die in 203 images in San Francisco, and Christian Sattler corrected and
  stitched them in the UK into a 17,265 × 14,313 picture.
- **What we used:** the page's 7000 × 5803 JPEG. The full-size master wasn't reachable (its
  folder returns HTTP 403), and there are no images of other layers, so all of this is read
  from the top surface only.
- **Which chip it is:** the package reads "SSI 263P / P 8404": a P part, not the later AP.
  (Reading 8404 as 1984, week 4 is an inference.) The only published difference between P
  and AP is a reset fix; a 1985 Usenet post reports a reset defect in a 263P design.
  Whether the Braille Lite's own chip is a P or an AP isn't known.
- The same page keeps the SSI-263A data sheets, the programming guide and an SC-02 scan.

## Casso's first read

Rob Elmer's Casso (an Apple II emulator, MIT) made the first read, for its Mockingboard
speech, and it holds up:

- **Where:** the ROM sits at about x 2350–3645, y 3100–3745 in the 7000-pixel picture.
- **Shape:** 64 columns, one per phoneme, and 43 row positions: 14 groups of two data rows
  and one empty spacer row, then one last data row. That's 29 data rows, named b00 to b27,
  plus one called "PAR" (a label from before its meaning was known; it is not a parity bit).
- **Order:** the column decoder's wire phases show the columns are mirrored: image column
  c holds phoneme code 63 − c.
- **A set bit** is a stadium-shaped oval contact in a light metal column; two set bits one
  above the other fuse into a double-height oval. 749 of the 1,856 cells are set.
- **The decode** (2026-08-26): the bits are significance-interleaved. Each group of rows
  holds one bit-plane of all six fields, not one field per group, the same layout MAME's
  work had shown for the SC-01. Read that way, the rows give six 4-bit fields plus five
  flag bits, and they line up with the SC-01A's decap: of 46 phonemes that pair by name,
  22 have identical F1, F2 and F3 codes, and the closure flag agrees on all 46.
- **What it borrowed:** the code-to-hertz table came from the SC-01's capacitor network.
  That is another chip's table, not an SSI-263 measurement.

## Two independent reads

Casso's read was checked by two more, made independently and compared only after both
were sealed.

**The rules:** each reader works from the original JPEG, records results by physical
column and row only (no phoneme names, no field labels), and seals the pass with hashes
before opening the other's. Both readers said up front what they already knew: the
general structure and the historical count of 749 ovals, and Astra had seen Casso's table.
What was independent was the measurement procedure, not the knowledge.

**Claude's read** (sealed 2026-09-23): the brightness autocorrelation gave the column pitch
(18.80 px) and a linear column fit (maximum residual 2.2 px). An oval-outline contrast
measure found the rows, and the spacer phase came out of the data: ovals only ever occur in
two of every three row positions. 1,821 cells were decided automatically and 35 by eye,
each with a written reason. All 896 spacer cells came out empty, as they must.

**Astra's read** (sealed 2026-09-24): an image-only lattice (64 tracks at 18.77 px, 2,752
sites), then sidewall contrast, template correlation, and a check that each label survives
a ±0.6-pixel shift. The first placement failed the spacer controls, and looking at the
pixels corrected it. 126 data cells and 14 spacers were reviewed by eye on contact sheets,
and five labels changed, each with a reason.

**The comparison:**

- Both found 749 ovals and 1,107 plain cells, but equal totals hid two disagreements, so
  they agreed on **1,854 of 1,856** data cells, and on all 896 spacers.
- Astra looked at the two disputed cells again in larger crops, judging the shapes only,
  not what the bits would mean. At column 32, row 3, the sidewalls continue through the
  paired row, so it is an oval. At column 35, row 30, there are horizontal bars but no
  enclosing sidewalls, so it is plain. Both came out as in Claude's read, and Claude
  confirmed.
- Neither sealed pass was edited; the agreed result is a separate file.
- Read with column = 63 − code and oval = 1, the agreed result matches **Casso's table on
  every one of the 1,856 cells**.

Three readers, three methods, one pattern. What that settles is the *visible* pattern.
It doesn't prove the decoder wiring, which way round the bits are, or what each field does.

## From cells to the engine

- `tools/make_rom_bits.py` turns the agreed read into `src/data/rom_bits.csv`: data rows
  only, code = 63 − column, oval = 1. Any difference from Casso's table is printed, not
  silently resolved.
- `src/ssi263/rom.py` keeps the raw bits apart from their *reading*, so another reading
  can be swapped in. The only reading so far is Casso's decode:

| Field | Rows, most significant first | What it is thought to be |
|---|---|---|
| FA | b05, b11, b17, b23 | noise (fricative) amplitude |
| VA | b06, b12, b18, b24 | voice amplitude |
| F3 | b07, b13, b19, b25 | filter 3 code |
| NAS | b08, b14, b20, b26 | nonzero only for M, N, NG and HN; b08 and b14 are always 0 |
| F2 | b09, b15, b21, b27 | filter 2 code |
| F1 | b10, b16, b22, PAR | filter 1 code (PAR as its lowest bit is disputed) |

| Flag | Name in rom.py | What the engine does with it |
|---|---|---|
| b00 | closure_clear | clear for exactly B, D, P, T, K, HVC and HFC: the closures |
| b01 | class1 | 1 = a true stop, which releases; 0 = the H-family holds, which stay shut |
| b02 | class2 | 0 for exactly S, Z, TH, THV and T: noise into F5; otherwise into F2 |
| b03 | not_fricative | matches the data sheet's fricative marks for 63 of 64 phonemes (PA the exception) |
| b04 | not_voiced | matches the voiced marks for 63 of 64 (PA again) |

Only the *groupings* of the flags are physical facts. What they do is inferred from the
recordings and from Tomi's listening; no wire has yet been traced from these rows to a
closure gate or a noise switch. Every one of the 1,856 bits in the engine matched the
agreed read in Astra's reviews of v0.2 and v0.9.

## What is still disputed

- **Is PAR F1's lowest bit?** For: E and AY, and AI and :A, differ only in PAR, which fits
  neighbouring F1 codes. Against, or not settled: Casso's own notes once called it
  non-acoustic; the capacitor fit on the recordings gives that bit a weight of 0.0 ± 0.3;
  and the Braille Lite's rules never send AY, AI or :A, so the recordings can't decide.
  The engine keeps both readings, switchable.
- **NAS → F2Q.** The design paper shows a separate F2Q control (the width of filter 2),
  and the nasal-only NAS field is the natural candidate. Untraced.
- **Where is F4?** The paper describes five sections with four control bits each; one of
  its figures lists F1, F2, F2Q, F3, F4, VA and FA and leaves out F5, while another draws a
  control on F5. The ROM decode has six fields and no F4. Both reviewers agreed not to
  relabel rows just to make the counts fit. The engine uses fixed placeholders at about
  0.163 × fc and 0.24 × fc, where the recordings show clock-scaled features.
- **The hertz scale.** Casso's hertz values are the SC-01's. With free bit weights, the
  recordings make F2 and F3 binary arrays (1:2:4:8) with candidate fixed-to-unit capacitor
  ratios of about 3.4, 1.75 and 4.1 for F1–F3, but none is measured on the die yet.
- **The link to a real chip.** The only direct link between this die's ROM and the chip in
  the Braille Lite is a rank check over 28 entries: F1 0.976, F2 0.975, F3 0.875. That rules
  out differences of several steps, but not of one, and says nothing about the other fields.

## Beyond the ROM

- **The analog map.** Astra marked five strongly repeated layouts along the die's left edge,
  B1 to B5, named by position only (not assigned to F1–F5), plus regions above, below and
  at the pads. B5 has square arrays on both sides, a candidate for a fixed section.
- **Surface traces.** In B5's left array, two groups of unit squares are joined by visible
  metal: 36 nodes and 34 links, which Claude reproduced exactly from its own crops before
  seeing Astra's overlay. A second pass, cross-checked, reached 55 nodes and 50 links.
  "20:16 is not a capacitor ratio": these are surface connections, not measured values.
- **The TP pins** (2026-09-25). Kraftwerk's Robovox patent (EP0396141A2) feeds outside
  audio into the chip's test pins, TP1 and TP2, which the data sheet says not to use.
  Astra's enlargements of both manufacturer block diagrams show both pins drawn into the
  glottal source; TP2 crosses the noise line with no junction dot. The patent's text
  mentions both pins, but its Figure 3 wires only TP2. So an "external carrier" replaces
  the glottal excitation; there is no sign of a separate noise input. Astra also traced
  seven visible-metal routes in three pad regions (with two possible underpasses marked
  unconfirmed); no pad has been matched to a package pin yet.
- **A lead for the dark J and SH.** An SC-01 patent (US4433210A) shows a switched-capacitor
  high-pass noise shaper, and gating signals that could explain the flag bits b03 and b04.
  These are shapes to look for on this die, not facts about it.

## Tools and records

- `tools/make_rom_bits.py`, `src/data/rom_bits.csv`, `src/ssi263/rom.py`: the ROM as the
  engine uses it.
- `third_party/casso/`: Casso's extraction, pinned at commit `7a10894` with its license.
- The sealed passes, the comparison, the adjudication and the trace packets are kept with
  the project's working files, outside this repository.
