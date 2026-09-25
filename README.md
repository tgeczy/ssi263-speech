# ssi263-speech

I'm Tamas, and I spend a lot of my spare time getting old speech synthesizers talking
again. This one is a little different: instead of one synthesizer, it's the chip that
sat inside a whole family of them.

The Silicon Systems **SSI-263** (Votrax sold it as the SC-02, Aicom called it the AI901)
was the voice of a lot of blind people's computers in the late 80s and 90s. This project
is a register-level emulation of that chip, and around it, the original firmware of four
talking devices, running unchanged and driving the emulated chip:

| Voice | Its own software | Runs in |
|---|---|---|
| **Speak-Out** talking box (GW Micro, 1995) | its NEC V40 firmware | Unicorn, inside NVDA |
| **Blazie Braille Lite 2000** in speech-box mode | the June 2003 firmware | z180emu |
| **Aicom Accent-mini** | Aicom's DOS device driver `SPKEMS.DVC` (Accent-EMS V4.5) | Unicorn, with emulated expanded memory |
| **Aicom Accent SA** | the box's own 8085 firmware and dictionary ROMs (1986–1989) | an emulated 8085 |

The pronunciation rules, the number reading, the intonation and the timing are all the
devices' own. The chip model makes every sample: nothing is recorded or concatenated.

## Why the chip, and not a recording

Every one of these boxes wrote the same five registers. If the chip is right, every box
that ever drove it can talk again, and each one sounds like itself, because its own rules
are doing the talking. So the chip comes first. It's built from the evidence, not by ear
alone:

- the phoneme ROM, read by Astra from [Visual6502.org's die shots](http://www.visual6502.org/images/pages/Silicon_Systems_SSI_263P_die_shots.html),
  and checked against Rob Elmer's Casso;
- the data sheet and the chip family's ISSCC paper;
- 78 minutes of my own Braille Lite 2000 recorded from its line out, used for development;
- a frozen hold-out set of other recordings that nothing is ever tuned on.

Where a measurement and my ears disagree, my ears win. So far they've been right every time.

## Getting the add-ons

[Download the latest release](https://github.com/tgeczy/ssi263-speech/releases/latest).
There are three NVDA add-ons, and they can all be installed together:

| Add-on | What you get | Settings |
|---|---|---|
| `speakout-ssi263` | Speak-Out | rate 0–9, pitch 0–9, tone A–Z as the variant |
| `blazie-ssi263` | Braille Lite / Braille 'n Speak | rate 1–16, pitch 1–63, tone 1–25 as the variant |
| `accent-ssi263` | two voices, **Accent-mini** and **Accent SA** | the Accent's own commands: rate R0–H, pitch P0–9, voice V0–9 as the variant, inflection from monotone to full |

They run on NVDA 2021.1 through 2026.1, 32- or 64-bit, on Windows 7 and later, and now on
Windows on ARM too. You don't need numpy or a Visual C++ runtime. All three support capital
pitch change. The Braille Lite and Accent have an option to read numbers as words, because
the Accent reads 100 as "one zero zero" and the Braille Lite stops short of trillions.

## About the firmware

The released add-ons carry each device's firmware, because without it there's no voice.
It isn't ours. It's here so these machines can speak again, and any of it will come down
if its rights holders ask.

This repository only keeps Aicom's software in the tree. Aicom stopped making the Accent
line around 2000, and by 2001 even its own customers couldn't find anyone to ask.
[`firmware/AICOM.txt`](firmware/AICOM.txt) has what we know and where each file came
from. The Speak-Out and Braille Lite firmware stay out of git: to build those two add-ons
yourself, put your own copies in `firmware/gw-micro-speakout/` and `firmware/blazie/`.
The README in each folder says which files, with their checksums.

## Documentation

- [How the SSI-263 learned to speak again](docs/history.md): the whole story, day by day,
  from the first ROM read to this release, and who found what along the way.
- [Reading the chip off its die](docs/die.md): how the phoneme ROM was read three separate
  ways from Visual6502's photographs, and what those bits do and don't tell us.
- [Measuring a real SSI-263](docs/measurements.md): how my own Braille Lite 2000 was
  recorded and measured, and the lessons that cost the most to learn.
- [The chip engine](docs/engine.md): what the model does, part by part, and what it
  doesn't do yet.
- [The four front ends](docs/front-ends.md): each device's own firmware, and the emulated
  machine it runs on.
- [Open questions](docs/open-questions.md): what we still don't know, and where the
  answer would come from.
- [Sources and credits](docs/sources.md): every data sheet, paper, patent, program and
  person this work stands on.

## Layout

| Path | What it is |
|---|---|
| `src/ssi263/` | The chip. `chip.py` is the reference, `params.py` holds every uncertain constant tagged by its source, `native.py` is the same chip in C behind the same interface |
| `src/csrc/` | The chip in C99, `build_native.py`, and the pinned Unicorn 2.1.4 source with its patches (see `UNICORN-ARM-FIX.md`) |
| `src/hosts/` | The firmware hosts: `speakout.py`, `blazie.py`, `accent.py`, `accent_sa.py` with `i8085.py`, and `ucmini.py` |
| `src/data/rom_bits.csv` | The phoneme ROM |
| `src/HOLDOUT.md` | The hold-out rules, and a log of every change to the chip and every look at the hold-out |
| `docs/` | The documentation (see above) |
| `tools/` | Research and check tools: comparisons against the recordings, the C/Python gate, renders |
| `nvda/` | The three NVDA drivers, their builds, and the driver test rig |
| `firmware/` | Aicom's software, and the places the other firmware goes |
| `third_party/casso/` | From Rob Elmer's Casso (MIT): the ROM extraction the die reading was checked against |

## Building

You'll need 64-bit Python 3.13 at `C:\Python313`, 32-bit at `C:\Python313-32`, and
w64devkit GCC (x86_64 at `C:\w64devkit`, i686 at `C:\w64devkit-x86\w64devkit`). The
research tools also want numpy, scipy, soundfile and librosa.

```
python src/csrc/build_native.py      # ssi263.dll, 64- and 32-bit, static
python nvda/build_speakout.py        # every build checks the C chip against chip.py first
python nvda/build_blazie.py          # also builds bns_live.exe from z180emu
python nvda/build_accent.py
```

The add-ons land in `nvda/dist/`. Unicorn's DLLs aren't rebuilt by those scripts: the
recipe is `UNICORN_BUILD` in `nvda/build_speakout.py`, and the x64 one carries the
Windows-on-ARM fix (`python src/csrc/build_unicorn_candidate.py`). The Braille Lite build
needs z180emu with this project's `bns.c` front end; its complete source ships inside the
Blazie add-on as `z180emu-source.zip`.

## Testing

| Command | What it checks |
|---|---|
| `python tools/check_native_core.py` | The C chip against `chip.py`: internal state exact, PCM identical, under every parameter switch |
| `python -S nvda/tools/driver_sim.py speakout\|blazie\|accent\|accentsa\|both "C:\Program Files\NVDA" tag` | A built driver against stand-in NVDA modules, importing only from NVDA's own library |
| `python nvda/tools/pitch_bug.py speakout\|blazie\|accent` | Capital pitch is never left raised |

## Credits

- **Engine, hosts and drivers:** Tamas Geczy (tgeczy), with Claude.
- **Visual6502.org:** the [SSI-263P die shots](http://www.visual6502.org/images/pages/Silicon_Systems_SSI_263P_die_shots.html)
  that everything here was read from. Greg James photographed the die in 203 images, and
  Christian Sattler corrected and stitched them into one 17265 × 14313 picture, from chips
  an anonymous donor sent them. The same page keeps the SSI-263A data sheets and
  programming guide. Without it there would be no ROM, and no chip.
- **The die:** Astra read the SSI-263's phoneme ROM from those shots, reviews the engine,
  and found the Windows-on-ARM fix.
- **Casso:** Rob Elmer, MIT (`third_party/casso/LICENSE`).
- **Unicorn 2.1.4:** GPLv2. Its source is pinned in `src/csrc/`.
- **z180emu:** GPLv2. The Braille Lite add-on ships its complete source.
- **The Accent SA ROMs** were shared by spacepup.
- **The firmware belongs to its makers:** the Speak-Out to GW Micro (hardware by Daniel
  Weirich, software by Douglas Geoffray), the Braille Lite to Blazie Engineering, the
  Accent to Aicom Corporation.
- The whole story, day by day and with who found what, is in [docs/history.md](docs/history.md).
- And thanks to everyone listening, testing and telling me what sounds wrong. That's how
  a chip model turns into a voice you can read with.

## License

The code in this repository is MIT: see [LICENSE](LICENSE). That covers our code only.
The firmware, Unicorn, z180emu and Casso keep their own terms, listed above.
