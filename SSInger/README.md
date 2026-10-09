# SSInger — SC-02 singing voice after the Robovox patent

A JUCE (9.0.3) instrument (VST3, CLAP, AU on macOS, LV2 on Linux, and a
standalone app; Windows, macOS universal, Linux x86-64 and arm64) that
recreates the Robovox system
from EP0396141A2 ("System for and method of synthesizing singing in real
time", Schneider / Ott / Jalass): a 6502 driving one to four Votrax SC-02
(= SSI-263) speech chips through a 6850 ACIA MIDI interface. The product
name is its own (Polaxis sells a unit called "Robovox" today); the patent
is credited everywhere it is used.

Start with [`RESEARCH.md`](RESEARCH.md) — the full patent/tour/hardware
analysis this build follows. The chip itself is this repository's existing
register-level model in `../src/csrc/ssi263.c`; nothing is recorded.

## Layout

| Path | What it is |
|---|---|
| `RESEARCH.md` | Patent mapping, 1998-tour deltas, SC-02 register map, emulator decisions, VST parameter table, open questions |
| `CMakeLists.txt` | JUCE 9.0.3 build, every format per OS (JUCE and clap-juce-extensions via FetchContent) |
| `plugin/` | `SSIngerProcessor` (APVTS + audio/MIDI glue); `SSIngerEditor`, the settings window, built for keyboard and screen-reader use (WCAG 2.2 AA applied to a plugin: named controls with real values, grouped, Tab order, focus ring, contrast; see SSIngerEditor.h) |
| `emu/mc6850.h` | Clean-room MC6850 ACIA model (MIDI subset: 8N1, Rx IRQ, RDRF/TDRE/OVRN) |
| `emu/ssinger_bus.h` | System bus: 6502 socket, 2 KB RAM (6116 footprint), ROM socket, ACIA, 2x74LS245 SC-02 buffer, mode switch |
| `emu/ssinger_firmware.h` | Clean-room translator: patent embodiment 1 (phoneme ch N, pitch ch N+1), inflection @ A=440 Hz, wheels, modes |
| `tests/` | Offline (no-JUCE) C tests: 6850 framing/IRQ, translator vectors, SSI-263 smoke render |
| `third_party/README.md` | Every third-party piece and its licence (JUCE, VST3/AU/LV2 SDKs, CLAP), floooh `chips` m6502, why not MAME |
| `../.github/workflows/ssinger.yml` | CI: builds on Windows, macOS (universal), Linux x86-64 + arm64; C tests, pluginval, auval, clap-validator; bundles as artifacts |

## Phase 1 status (this commit)

- MIDI → SC-02 register translation runs in C, per the patent's first
  embodiment, through the emulated 6850's real status/RDRF/IRQ path.
- It is a **two-track instrument**: the phoneme channel alone sings at
  ~30 Hz (inflection 0) — put pitch notes on channel N+1 (or a second
  clip) or you will hear almost nothing on small speakers. That is the
  patented method, not a bug.
- The key→phoneme layout is fixed and built in (`note_map.txt`, in the download, documents
  it — reference only, the plugin reads no files). A custom layout becomes
  a UI feature (Phase-2 PEC-style editor), not a sidecar file. All 64
  chip phonemes are playable: notes 36–89 (C2–F6) sing E to TH in chip
  order, 90–93 are PA (the patent's spare keys), and 94–102 (A#6–F#7) sing
  the nine 0.7.0 left out: M, N, NG, :A, :OH, :U, :UH, E2, LB. No earlier
  key moved. In Expander mode, Program Change N sings note N's phoneme.
- The 6502 socket, memory map, IRQ line and ROM image slot exist and are
  exercised by the tests; the 6502 core itself (floooh `chips` `m6502.h`,
  MIT) drops in via `third_party/` without touching the translator API.
  Moving the translator into a 6502-resident ROM image is the explicit
  Phase-2 milestone — the bus is ready for it.
- Single-chip (SEQ) and quad-chip (tour rig, channels 1/3/5/7 + 2/4/6/8)
  configurations. External-carrier input = second audio input pair.
- Tour rig: the filter control (the mod wheel; the pitch wheel under the
  Patent wheel map) moves all four chips at once, from any of their
  channels. "Chip 2/3/4 filter offset (tour rig)" set each chip's filter
  in steps from chip 1's ("Filter frequency (FF)"), so the wheel sweeps
  the four together and keeps their spread; with one chip they do
  nothing. Four chips add at full level (one chip sounds as loud as SEQ)
  through a fixed soft knee that keeps four in unison under 0 dBFS
  (0.7.0 divided by four, 12 dB quieter).

## System requirements

| OS | Runs on | Why |
|---|---|---|
| Windows | **Windows 10 (version 1607) or later**, x64. Not Windows 7 or 8.1: the binaries will not load there. | JUCE 8 and 9 support Windows 10 1607+ only; JUCE imports dcomp.dll, the shcore scaling API and user32's per-monitor DPI functions directly. JUCE 9 has no supported switch to lower this. |
| macOS | **macOS 10.15 (Catalina) or later**, Apple silicon or Intel (one universal binary) | Built with `CMAKE_OSX_DEPLOYMENT_TARGET=10.15` |
| Linux | x86-64 or arm64 with **glibc 2.35 and libstdc++ from GCC 12 or newer**: Ubuntu 22.04+, Debian 12+, Fedora 36+, Raspberry Pi OS Bookworm or later. Older systems (Debian 11, Ubuntu 20.04): build from source. | The downloads are built on GitHub's Ubuntu 22.04 runners; their binaries ask for GLIBC_2.35, GLIBCXX_3.4.30 and CXXABI_1.3.13 (checked on this build) |

## Build

Full instructions per OS (Windows/MSVC, macOS/Apple clang universal,
Linux/GCC), the install folder for every format, the macOS Gatekeeper
step, and how to use SSInger with a screen reader in each DAW are in
[`BUILD.md`](BUILD.md). In short, from the repo root:

```
cmake -S SSInger -B build/SSInger -DCMAKE_BUILD_TYPE=Release
cmake --build build/SSInger --config Release --target SSInger_VST3
```

Tests first (no network needed):

```
cmake -S SSInger/tests -B build/SSInger-tests
cmake --build build/SSInger-tests --config Release
ctest --test-dir build/SSInger-tests -C Release
```

Every control is a host parameter with a plain name and value text, so a
DAW's generic parameter view (REAPER + OSARA's FX parameter list, Logic's
Controls view, Ardour's generic controls) plays SSInger fully; on Linux,
where JUCE has no screen-reader support, that is the way in.

## Licenses

Our code (including the clean-room 6850 and translator): MIT, like the
rest of this repository. JUCE (AGPLv3/commercial), the VST3 SDK (MIT),
Apple's AudioUnitSDK (Apache-2.0), the LV2 headers (ISC),
clap-juce-extensions and CLAP (MIT) and floooh `chips` (MIT) keep their own
terms — see `third_party/README.md`. Plainly: the source here is MIT;
binaries built with JUCE are AGPLv3 (JUCE's terms set the binary licence)
in every format and on every OS, unless built under JUCE's commercial
licence. The
Robovox patent EP0396141A2 is withdrawn; the original Robovox
firmware/Atari software is lost, so the translator here is a new
implementation of the patent text.
