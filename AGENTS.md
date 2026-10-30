# AGENTS.md

Guidance for AI coding agents (and the people driving them) working on ssi263-speech. Read this before you change
anything or open a pull request.

## What this project is

A register-level emulation of the Silicon Systems **SSI-263** speech chip (also sold as the Votrax SC-02 and Aicom
AI901), and around it the original software of the talking devices that drove it, running unchanged on emulated
CPUs: the Blazie Braille Lite, Braille 'n Speak and Type 'n Speak, GW Micro's Speak-Out and Sounding Board, Aicom's
Accent SA and Accent-mini, Sweet Micro Systems' Mockingboard, and Automated Functions' SmarTalk. Every sample comes
from the chip model; nothing is recorded or concatenated. The devices' own rules, numbers, intonation and timing do
the talking.

The chip comes first. If the chip is right, every box that drove it sounds like itself. Changes to the chip model
are made from evidence (the die, the datasheet, recordings of real chips), never by ear alone and never to make one
device sound nicer at the others' expense.

## How the repository is organized

| Folder | What is there |
|---|---|
| `src/ssi263/` | the Python reference chip model; the C chip (`src/csrc/ssi263.c`) is held to it |
| `src/csrc/` | the portable C core every platform builds: the chip, each device's board and host (`blazie/`, `speakout/`, `soundingboard/`, `accentsa/`, `accentmini/`, `mockingboard/`, `smartalk/`), the CPU cores (`cpu/`, `pc86/`), and the voice table (`voices.c`) |
| `src/hosts/` | the Python research hosts: each device's host as first written, the reference its C port is checked against |
| `src/apps/` | the Blazie emulator apps (Windows, GTK, Linux terminal) and `blazie_files` |
| `src/platforms/` | Android, Apple, BT Speak / BT Braille, speech-dispatcher |
| `nvda/` | the NVDA add-ons, their builders, and `nvda/tools/run_tests.py`, the Windows test suite |
| `sapi/` | the SAPI 5 engine and installer |
| `python/` | the Python wheel |
| `SSInger/` | the singing plug-in |
| `tools/` | checks, comparisons and release scripts; `tools/linux_tests.sh` is the Linux suite |
| `docs/` | the engine, the die, the front ends, measurements and open questions |
| `firmware/` | **drop-in folders only**: a README per device saying which files go there, with their sha256 |

Each folder has a README that says what its files do. Keep it that way: one job per file, and a README updated with
the code.

The C core is a line-for-line port of the Python reference hosts where one exists: same order of operations, same
rounding, same register accesses. When you change one, change the other in the same commit, and the C-against-Python
check must still pass write for write.

## How we work: tests first, ears last

**Test-driven.** A fix starts with a test that fails on the bug. A feature starts with the test that says what it
must do. Each test that guards a behaviour comes with a **must-fail control**: an environment switch (the repo's
`*_BREAK` variables) that puts the bug back or breaks the behaviour, registered in the suite so the control is
proven to fail. A test whose control passes is not testing anything; fix the test.

Prefer one solid test over ten look-alikes, and keep the suites fast: `nvda/tools/run_tests.py` runs a check per
physical core (`SSI263_TEST_JOBS` to change it) and should finish in a few minutes. `--quick` runs every check and
no control: the everyday run. Run the full suite when you add or change a test or the code a control breaks, and
before every release. A check that times something in wall-clock milliseconds is registered `alone=True` and runs
after the others, one at a time. Long runs go in the background, never in the foreground of a session.

**The human ear is the final gate.** Numbers can be right and the voice still wrong. No change to how anything
sounds is done until a person has listened to it on the real add-on or app and agreed. When a measurement and the ear
disagree, the ear has won every time so far; go back and find what the measurement missed.

**Evidence rules.**
- Recordings named in `src/holdout_lines.txt` and `src/HOLDOUT.md` are held out. Never tune on them; check against
  them only.
- Say what a result rests on, and what was not tested. "Not run" is a fine answer; a guess presented as a result is not.
- When an analysis stalls, stop and report what you found rather than trying variation after variation.

## Firmware and other people's code

The devices' firmware, ROMs, drivers and disk images belong to their makers. **This repository does not take
firmware from contributors.**

- **A pull request that adds firmware, a ROM, a driver binary, a state file or a disk image is rejected**, however it
  is named or packed. Point to the file's drop-in folder and its sha256 instead; the code must work when the file is
  there and say clearly what is missing when it is not.
- **Issue comments and attachments carrying firmware are removed.** Describe the file (name, size, sha256, where it
  came from) instead.
- The maintainers alone decide what, if anything, is shipped, and under which notice.
- The phone apps (Android, Apple) carry no firmware at all; their packaging checks refuse firmware by content and
  sha256. Do not weaken those checks.

**Licences.** The project is MIT. Its CPU cores are MAME's BSD-3-Clause versions. No GPL code may ship: a test fails
if any reaches a build, and Unicorn and z180emu are development references only. Do not add a dependency without its
licence and a reason.

## Pull requests

- **Include new tests**, with their must-fail controls, registered in `nvda/tools/run_tests.py` (and in
  `tools/linux_tests.sh` when they run on Linux).
- **Say exactly what you ran**: which checks, on which platform, with which firmware present, and what you did not
  run. If the whole suite was not run, say so.
- **A human is in the loop for the final test.** A maintainer runs the full suites and listens to the change before
  it is merged. Write the description so they can: what changed, how to hear it, and where it could go wrong.
- Keep a pull request to one subject. Rebase on main; do not reformat code you did not change.
- **No machine paths** (anything naming a person's own folders or drives) in code, docs or commit messages. Use paths relative to the
  repository, or the existing `paths.local` mechanism.
- **No one's personal handle in commits.** Credit people by first name, and only once they have agreed to it.
- Write code that reads like the code around it: its naming, its comment density, its idiom. Comments say why,
  and cite the evidence (a measurement, a register, a test) when there is one.

## Building and testing

- Windows: `python src/csrc/build_ssi263speech.py`, the add-on with `python nvda/build_ssi263.py` (it runs the
  units' `build_blazie.py`, `build_speakout.py` and `build_accent.py` itself), then `python nvda/tools/run_tests.py`
  (`--quick` for every check without the controls). Toolchain paths go in `paths.local` (see `paths.local.example`).
- Linux: `./build_linux.sh`, then `sh tools/linux_tests.sh <firmware folder>`.
- Android: `build_android.sh` for the native library, then Gradle; Apple: Xcode on a Mac.
- Without the firmware in its drop-in folder, the checks that need it skip and say so; that is not a pass for the
  behaviour they check.
