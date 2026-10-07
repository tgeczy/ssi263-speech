"""Build the Blazie emulator for Windows, into nvda/dist/blazie-emu/:

  blazie_emu.exe   the app (one static program: the chip, the boards on MAME's Z180, the host, the shell)
  test_chords.exe  the chord logic's tests (run_tests runs it)
  test_emu_unit.exe  the unit, headless: boot speech, a chord answered, real-time speed
  test_clock.exe     the clock controller and the keys held while the unit starts (Jayson), headless
  test_flash.exe     the file flash: the ID check, the erase's time and chirps, files kept; _break, _old: its controls
  test_files.exe     files in and out (bl_files.c, the FAT image) against the units' own commands
  blazie_files.exe   the same on a saved state, from the command line
  test_games.exe     Blazie's games (Simon, Hangman) run from the unit's files, headless (RetroBunn: Simon)
  test_serial.exe    the serial port plugged in, headless: the storage handshake answered from the far end
  test_serial_cut.exe  the same with the receive path cut: its "ACK answered" must FAIL (run_tests' control)
  test_serial_win.exe  the Windows COM side (serial_win.c) end to end through a named pipe; _cut: its control
  test_idle.exe    the idle channel against Tomi's unit (its board built with bl_idle.c's test hooks)
  test_audio.exe   the sound queue (audio_pace.c) against a simulated sound card; --old: its must-fail control
  make_state.exe   a unit's factory state from its firmware (the Braille 'n Speak 2000's: bl_state.c's recipe)
  test_bns.exe     the Braille 'n Speak 2000 on the board: the units told apart, its words through the chip's writes
  LICENSE, licenses/  the project's MIT, MAME's BSD-3-Clause notice for the Z180 core, Casso's MIT

The boards run on MAME's Z180 (src/csrc/cpu/z180_mame.cpp, BSD-3-Clause), built as build_board.py builds the release
bl.dll: every source compiled on its own, the board with -DBL_Z180_MAME, linked by g++ with libstdc++ and libgcc
static.  No z180emu (GPL) in any program here (Tomi: the emulator MIT for 0.7): python tools/check_no_gpl.py
nvda/dist/blazie-emu.  A test's must-fail control gets its define on just the file that reads it.

w64devkit gcc, x64 (paths.local W64DEVKIT).  The firmware is NOT copied: a release puts firmware\\ beside the program;
run from the source tree, the program finds firmware/blazie/ itself.

    python src/apps/blazie/build_app.py
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, REPO)
from tools import repo_paths  # noqa: E402

CSRC = os.path.join(REPO, "src", "csrc")
BLAZIE = os.path.join(CSRC, "blazie")
CPU = os.path.join(CSRC, "cpu")
OUT = os.path.join(REPO, "nvda", "dist", "blazie-emu")
CHIP = ["-O2", "-std=c99", "-ffp-contract=off", "-Wall"]
# the boards and host as the release bl.dll's (build_board.py's build_mame_dll); MAME's core as its C++17
INC = ["-I" + BLAZIE, "-I" + CPU, "-I" + CSRC]
BOARD = ["-O3", "-std=gnu89", "-ffp-contract=off", "-DBL_Z180_MAME", "-w"] + INC
MAME_CXX = ["-O3", "-std=c++17", "-fno-exceptions", "-fno-rtti", "-ffp-contract=off", "-Wall"] + INC
APP = ["-O2", "-std=c99", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-format-truncation"]
LINK = ["-static", "-static-libstdc++", "-static-libgcc", "-s"]

# the board's files (each its own object, as bl.dll's) and the define each control puts on just the file reading it
BOARD_FILES = ("bl_board", "flash29", "bl_serial", "bl_idle", "bl_clock", "bl_host", "tns_board")
CONTROLS = {
    "flash_break": {"bl_board": "BLAZIE_FLASH_BREAK", "tns_board": "BLAZIE_FLASH_BREAK"},
    "cut_rx": {"bl_serial": "BL_SERIAL_CUT_RX"},
    "hooks": {"bl_idle": "BLI_TEST_HOOKS"},
    "flash_old": {"flash29": "FLASH29_NO_PREPROGRAM"},
}
LICENCES = (("LICENSE", os.path.join(REPO, "LICENSE")),
            (os.path.join("licenses", "MAME-Z180-core-BSD-3-Clause.txt"),
             os.path.join(CPU, "mame_z180", "LICENSE-BSD-3-Clause.txt")),
            (os.path.join("licenses", "Casso-MIT.txt"), os.path.join(REPO, "third_party", "casso", "LICENSE")))


def main():
    bindir = repo_paths.bin_dir("W64DEVKIT", path_fallback=True)
    gcc, gxx = os.path.join(bindir, "gcc.exe"), os.path.join(bindir, "g++.exe")
    env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"])
    # a fresh folder: an object or program left by an older build (z180emu's bl_unity) must never come back in
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    obj = os.path.join(OUT, "obj")
    os.makedirs(obj)

    def run(argv):
        subprocess.run(argv, env=env, check=True)

    def compile_c(src, flags, name=None, defines=()):
        o = os.path.join(obj, (name or os.path.splitext(os.path.basename(src))[0]) + ".o")
        run([gcc] + flags + ["-D" + d for d in defines] + ["-c", src, "-o", o])
        return o

    def link(exe, objs, libs=("-lm",), extra=()):
        run([gxx] + LINK + list(extra) + ["-o", os.path.join(OUT, exe)] + list(objs) + list(libs))

    core = []
    for name in ("z180_mame", "z180_asci"):
        o = os.path.join(obj, name + ".o")
        run([gxx] + MAME_CXX + ["-c", os.path.join(CPU, name + ".cpp"), "-o", o])
        core.append(o)
    chip = [compile_c(os.path.join(CSRC, "ssi263.c"), CHIP), compile_c(os.path.join(CSRC, "ssi263dsp.c"), CHIP)]

    def board(control=None):
        """The board's objects; with a control, its define on just the files that read it."""
        defs = CONTROLS.get(control, {})
        objs = []
        for f in BOARD_FILES:
            src = os.path.join(BLAZIE, f + ".c")
            if f in defs:
                objs.append(compile_c(src, BOARD, name="%s_%s" % (f, control), defines=(defs[f],)))
            else:
                o = os.path.join(obj, f + ".o")
                objs.append(o if os.path.isfile(o) else compile_c(src, BOARD))
        return objs + core

    # the units' files in and out (Tomi): their file systems, the FAT image, the state files
    files = [compile_c(os.path.join(BLAZIE, f + ".c"), APP) for f in ("bl_files", "bl_files_state", "bl_files_xfer",
                                                                      "fat_img")]
    emu = compile_c(os.path.join(HERE, "emu_unit.c"), APP)
    chords = compile_c(os.path.join(HERE, "chords.c"), APP)
    # the Type 'n Speak's factory setup, headless: its cold reset's questions answered (the tests, the rescue)
    setup = compile_c(os.path.join(HERE, "tns_setup.c"), APP)
    unit = chip + board() + [emu, chords, setup]
    # a saved Type 'n Speak that was never set up (the previews' first start), told apart and set up anew
    rescue = compile_c(os.path.join(HERE, "tns_rescue.c"), APP)
    shell = [compile_c(os.path.join(HERE, f), APP) for f in ("main_win.c", "tns_keymap_win.c", "serial_win.c",
                                                             "audio_pace.c")]
    # the sound queue against a simulated sound card (Tomi: the emulator's speech stutters); --old: its control
    link("test_audio.exe", [compile_c(os.path.join(HERE, "test_audio.c"), APP), os.path.join(obj, "audio_pace.o")],
         libs=())
    link("blazie_emu.exe", unit + shell + files + [rescue], libs=("-lwinmm", "-lsetupapi", "-lcomdlg32", "-lm"),
         extra=("-mwindows",))
    link("test_rescue.exe", [compile_c(os.path.join(HERE, "test_rescue.c"), APP), rescue] + unit + files)
    link("test_chords.exe", [compile_c(os.path.join(HERE, "test_chords.c"), APP), chords], libs=())
    # the same from the command line, on a saved state (no unit runs: Linux and the BTSpeak build it alone)
    link("blazie_files.exe", [compile_c(os.path.join(HERE, "blazie_files.c"), APP)] + files, libs=())
    # the unit, headless (run_tests passes it the firmware)
    link("test_emu_unit.exe", [compile_c(os.path.join(HERE, "test_emu_unit.c"), ["-O2"])] + unit)
    # the clock controller and the keys held while the unit starts (Jayson), headless
    link("test_clock.exe", [compile_c(os.path.join(HERE, "test_clock.c"), APP)] + unit)
    # the file flash (test_flash.c), and its control: the boards built with BLAZIE_FLASH_BREAK (the Type 'n Speak's
    # chip answers a 29F040's ID; the Braille Lite's banks all on one 512 KB)
    flash = compile_c(os.path.join(HERE, "test_flash.c"), APP)
    link("test_flash.exe", [flash] + unit)
    link("test_flash_break.exe", [flash] + chip + board("flash_break") + [emu, chords, setup])
    # ... and the erase without its preprogramming (flash29.c, FLASH29_NO_PREPROGRAM: 32 s, as before)
    link("test_flash_old.exe", [flash] + chip + board("flash_old") + [emu, chords, setup])
    # files in and out against the units' own commands (test_files.c; its controls: --break=N)
    link("test_files.exe", [compile_c(os.path.join(HERE, "test_files.c"), APP)] + unit + files)
    # Blazie's games run from the unit's files (test_games.c; RetroBunn, PR #9: Simon); its control: TEST_GAMES_BREAK
    link("test_games.exe", [compile_c(os.path.join(HERE, "test_games.c"), APP)] + unit + files)
    # the serial port plugged in, and its control: the same board with the receive path cut (bl_serial.c)
    serial = compile_c(os.path.join(HERE, "test_serial.c"), APP)
    unit_cut = chip + board("cut_rx") + [emu, chords, setup]
    link("test_serial.exe", [serial] + unit)
    link("test_serial_cut.exe", [serial] + unit_cut)
    # the Windows side end to end through a named pipe (serial_win.c), and its control on the cut board
    serial_win = compile_c(os.path.join(HERE, "test_serial_win.c"), APP)
    win = os.path.join(obj, "serial_win.o")
    for exe, objs in (("test_serial_win.exe", unit), ("test_serial_win_cut.exe", unit_cut)):
        link(exe, [serial_win, win] + objs, libs=("-lsetupapi", "-lm"))
    # the Braille 'n Speak 2000 (Tomi: the Slovak firmware): its factory states made from its firmware by the state
    # recipe (bl_state.c), and its checks on the board -- the units told apart, its words through the chip's writes
    recipes = [compile_c(os.path.join(BLAZIE, f + ".c"), BOARD) for f in ("bl_state", "bl_firmware")]
    link("make_state.exe", [compile_c(os.path.join(HERE, "make_state.c"), APP)] + recipes + chip + board())
    link("test_bns.exe", [compile_c(os.path.join(HERE, "test_bns.c"), APP)] + recipes + chip + board())
    # the idle channel's sounds against the unit's (run_tests passes the firmware); its board built with the test hooks
    # its must-fail controls use (bl_idle.c, BLI_TEST_HOOKS)
    link("test_idle.exe", [compile_c(os.path.join(HERE, "test_idle.c"), ["-O2"])] + chip + board("hooks"))
    # the licences: MIT (ours), MAME's BSD-3-Clause for the Z180 core, Casso's MIT (the chip model draws on it)
    for dst, src in LICENCES:
        os.makedirs(os.path.dirname(os.path.join(OUT, dst)) or OUT, exist_ok=True)
        shutil.copyfile(src, os.path.join(OUT, dst))
    print("built %s" % OUT)


if __name__ == "__main__":
    main()
