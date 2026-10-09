# src/csrc/mockingboard: Sweet Micro Systems' Mockingboard, speaking through our SSI-263

**Work in progress for 0.8.** The Mockingboard (Sweet Micro Systems, 1983; the Apple II's best-known sound and
speech card) with its SSI-263, driven by Sweet Micro's own text-to-speech: Mike LePage's version 1.1 of 11 March
1985, from the Mockingboard Developers Toolkit disk. Its letter-to-sound rules are Sweet Micro's (in the NRL style,
like Blazie's and GW Micro's but their own), so this voice says words its own way.

Nothing of Apple's is used: no monitor ROM, no DOS, no Applesoft. Sweet Micro's six program and rule files run on
our own 6502 (`../cpu/m6502.c`, Fake6502's instructions via Jayson's EchoTalk) and our own small Apple II and
Mockingboard (`mb_board.c`), with sixteen bytes of "ROM" of our own.

| File | What it is for |
|---|---|
| `mb_board.h`, `mb_board.c` | The machine: 48 KB of RAM, the 16 KB language card (its switches at C080h-C08Fh), the SSI-263 at C440h-C447h, the second VIA's PCR, IFR and IER with the chip's A/R on CA1, and our "ROM" (an IRQ entry that does what the monitor's does for the driver, an idle loop, the vectors). The host's guard (writes into a range counted) and abort. |
| `mb_host.h`, `mb_host.c` | The host: the firmware file placed as the demo BLOADs it (only the known set, by sha256), the four settings bytes, a text said as the MB$ path leaves it (8500h, a space before and after), the rules run with the chip's time standing still, then every frame played from the A/R interrupt in lockstep with the chip (accent_sa.py's lockstep); the overflow guard; cancel by the driver's own end-of-text path. |
| `mb_render.c` | The C API alone: the firmware folder and a text into a WAV (`mb_render <folder> "text" out.wav [--log]`). |
| `test_mockingboard.c` | The host on the real firmware (9 tests): the golden R0 frames, the five register streams and the settings in them, a clean core (no decimal mode, no undocumented opcode), rate and inflection, refusals and the length limit, the overflow guard, cancel, a changed byte refused. Skips (77) without the firmware file. |
| `mb_controls.py` | Its must-fail controls (6): each rule undone in a scratch copy, exactly its tests must fail. |

`../../../tools/mockingboard_firmware.py` makes the firmware file, `mockingboard-tts-1.1.bin`, from the toolkit's
disk image: the six DOS 3.3 binary files the voice runs (TEXT TO SPEECH, INFLECTION, IIE TTS DRIVER and the three
MKB:RULE files), back to back, each exactly as DOS stores it. It refuses any other disk.

## What the firmware does, and the host's numbers

- **The interface** (from the toolkit's own DEMO and MB$ GETTEXT): the text at (06h), its last index at 8C03h, then
  JSR 8C11h. The rules make R0 frames; INFLECTION makes the R1-R4 streams from the four settings at 92DCh-92DFh
  (inflection 0-26, the starting pitch; rate; amplitude; filter frequency) and from the punctuation; the IIE TTS
  DRIVER then plays one frame (R4, R3, R2, R1, R0) at each A/R interrupt, while 1Eh is FFh.
- **Two limits of the firmware's own**: a text of at most 253 characters (its index is one byte and its loops run
  while it is at most the last index), and at most 256 frames (its five streams are 256-byte pages; past them the R0
  frames run into 8B00h and then the program). The host refuses a text that would pass the second before anything
  plays (`MBH_FAULT_LONG`); the voice splits it.
- **Rates 14 and 15** wrap in INFLECTION's rate + 2 and come out slow: the voice uses 0-13.
- **`MBH_XCK_HZ` 1,022,727 is a guess**: the chip's clock taken from the Apple's bus clock, as MAME clocks the SC-01
  on the same card. The schematic has not been checked.
- **A host feature**: a text's conversion runs with the chip's time standing still, so speech starts at once.

## Evidence

The same R0 frames for the same text on two independent 6502s: py65 (a scratch prototype) and this core. The
firmware never reaches decimal mode or an undocumented opcode in any test text. Tested against nothing real yet:
recordings of a real Mockingboard would settle the clock and the analog side.

## Open

- The voice (`mb_voice`, settings on NVDA's scales, splitting texts) and its entry in `../voices.h`.
- The firmware file into `firmware/` with its notice, once its provenance ledger is written.
- The earlier SSI-263 version on the Mockingboard C's own disk (one program at 6600h, a different rule table):
  a second voice only if it sounds different.
