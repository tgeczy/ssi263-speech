"""blazie_emu on a BT Speak or BT Braille (bt_handover.c): the one emulator finds the device and hands over to the BT
frontend, blazie_emu_bt, installed beside it (Tomi: one emulator, no separate flavour for the BT devices); with
[input] bt = native it uses the device's keyboard and display itself, with no hand-over.

    python3 src/apps/blazie/test_bt_handover.py BLAZIE_EMU

The detection, through blazie_emu --bt-probe with a stand-in BTSpeak library on PYTHONPATH whose keyboard service is
a file: the library and the service there is a BT device; either one missing is not.  And this machine itself, which
is not a BT device (set BLAZIE_TEST_ON_BT_DEVICE=1 to run this on one, where it must say yes).
The hand-over, through a copy of blazie_emu with a stand-in blazie_emu_bt (and blazie_bt) beside it, the device
answered by BLAZIE_BT_DETECT: the stand-in runs with --unit, --firmware, --config as --state-dir and --rate, and
nothing else; --no-bt, [input] bt = off, bt = native and no device keep this program (it runs, and stops at its
missing firmware); with the frontend missing, one line says so and this program runs.
The control: BLAZIE_BT_BREAK=1 ignores --no-bt and the setting and loses --unit: four checks must FAIL
(tools/linux_tests.sh judges it by its marks).
"""
import os
import shutil
import subprocess
import sys
import tempfile

failures = 0
ran = 0
FOUND = "BT Speak or BT Braille detected: using its keyboard, speech and braille display."
MISSING = "BT Speak or BT Braille detected, but blazie_emu_bt is not installed beside blazie_emu"
STUB = """#!/bin/sh
for a in "$@"; do printf '%s\\n' "$a"; done > "$BT_STUB_ARGS"
echo "stand-in frontend ran"
"""
# a stand-in for the device's library: its keyboard service is "available" when the file named there exists
KB_CLIENT = """import os
def server_available():
    return os.path.exists(os.environ.get("BLAZIE_TEST_BT_SERVICE", "/nonexistent"))
"""


def check(name, ok, detail):
    global failures, ran
    print("%-4s %-44s %s" % ("ok" if ok else "FAIL", name, detail))
    failures += not ok
    ran += 1


def env_without_bt(**extra):
    env = {k: v for k, v in os.environ.items() if k not in ("BLAZIE_BT_DETECT", "BLAZIE_TEST_BT_SERVICE")}
    env["BLAZIE_BTKB_SOCKET"] = "/nonexistent/keyboard-socket"   # never the real device's keyboard (a BT device)
    env.update(extra)
    return env


def run(cmd, env):
    p = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       universal_newlines=True, env=env, timeout=60)
    return p.returncode, p.stdout


def detection(emu, tmp):
    lib = os.path.join(tmp, "fake-bt-lib")
    os.makedirs(os.path.join(lib, "BTSpeak"))
    open(os.path.join(lib, "BTSpeak", "__init__.py"), "w").close()
    with open(os.path.join(lib, "BTSpeak", "kb_client.py"), "w") as f:
        f.write(KB_CLIENT)
    service = os.path.join(tmp, "keyboard-service")
    open(service, "w").close()
    path = os.environ.get("PYTHONPATH", "")
    with_lib = lib + (os.pathsep + path if path else "")
    for name, env, want in (
            ("detection: the BTSpeak library and its keyboard service",
             env_without_bt(PYTHONPATH=with_lib, BLAZIE_TEST_BT_SERVICE=service), 0),
            ("detection: no BTSpeak library", env_without_bt(BLAZIE_TEST_BT_SERVICE=service), 1),
            ("detection: no keyboard service",
             env_without_bt(PYTHONPATH=with_lib, BLAZIE_TEST_BT_SERVICE=service + ".absent"), 1)):
        code, out = run([emu, "--bt-probe"], env)
        check(name, code == want, "exit %d, %s" % (code, out.strip()))
    on_bt = os.environ.get("BLAZIE_TEST_ON_BT_DEVICE") == "1"
    code, out = run([emu, "--bt-probe"], env_without_bt())
    check("detection: this machine (%s)" % ("a BT device" if on_bt else "not a BT device"),
          code == (0 if on_bt else 1), "exit %d, %s" % (code, out.strip()))


def install(emu, folder, frontend=True):
    """a copy of blazie_emu, with the stand-in frontend and worker beside it"""
    os.makedirs(folder)
    copy = os.path.join(folder, "blazie_emu")
    shutil.copy2(emu, copy)
    if frontend:
        for name, text in (("blazie_emu_bt", STUB), ("blazie_bt", "#!/bin/sh\nexit 0\n")):
            with open(os.path.join(folder, name), "w") as f:
                f.write(text)
            os.chmod(os.path.join(folder, name), 0o755)
    return copy


def config(folder, bt=None):
    """a settings file that keeps the terminal emulator off the input devices and the braille display (the test
    machine's own)"""
    os.makedirs(folder)
    with open(os.path.join(folder, "blazie_emu.ini"), "w") as f:
        f.write("[input]\nevdev = off\n" + ("bt = %s\n" % bt if bt else "") + "\n[btspeak]\ndisplay = off\n")
    return folder


def hand_over(emu, tmp):
    copy = install(emu, os.path.join(tmp, "bin"))
    alone = install(emu, os.path.join(tmp, "alone"), frontend=False)
    args_file = os.path.join(tmp, "stub-args")
    fw = os.path.join(tmp, "firmware folder")      # a space in it: the argument passed whole
    cfg = config(os.path.join(tmp, "config"))
    cfg_off = config(os.path.join(tmp, "config-off"), bt="off")
    cfg_native = config(os.path.join(tmp, "config-native"), bt="native")
    xdg = os.path.join(tmp, "xdg")                 # the usual settings, for a run given no --config
    config(os.path.join(xdg, "ssi263-speech", "blazie-emu"))
    missing_fw = os.path.join(tmp, "no-firmware")

    def go(exe, args, detect):
        if os.path.exists(args_file):
            os.remove(args_file)
        code, out = run([exe] + args, env_without_bt(BLAZIE_BT_DETECT=detect, BT_STUB_ARGS=args_file,
                                                     XDG_CONFIG_HOME=xdg))
        got = open(args_file).read().splitlines() if os.path.exists(args_file) else None
        return code, out, got

    code, out, got = go(copy, ["--unit", "bl-es", "--firmware", fw, "--config", cfg, "--rate", "22050"], "1")
    want = ["--unit", "bl-es", "--firmware", fw, "--state-dir", cfg, "--rate", "22050"]
    check("hand-over: the device found, its options", got == want and FOUND in out and code == 0,
          "ran with %s, exit %d%s" % (got, code, "" if FOUND in out else ", the line not said"))
    code, out, got = go(copy, [], "1")
    check("hand-over: nothing given, nothing passed", got == [] and FOUND in out, "ran with %s" % got)
    terminal = ["--firmware", missing_fw, "--no-sound"]
    for name, exe, args, detect in (
            ("--no-bt: the terminal emulator", copy, ["--no-bt", "--config", cfg, "--unit", "bl-en"] + terminal, "1"),
            ("[input] bt = off: the terminal emulator", copy, ["--config", cfg_off, "--unit", "bl-en"] + terminal,
             "1"),
            ("[input] bt = native: no hand-over", copy, ["--config", cfg_native, "--unit", "bl-en"] + terminal, "1"),
            ("no device: the terminal emulator", copy, ["--config", cfg, "--unit", "bl-en"] + terminal, "0")):
        code, out, got = go(exe, args, detect)
        ran_terminal = "Could not start the Braille Lite 2000 (English)" in out
        check(name, got is None and FOUND not in out and ran_terminal,
              "frontend %s, terminal emulator %s" % ("ran with %s" % got if got is not None else "not run",
                                                     "ran" if ran_terminal else "did not run"))
    code, out, got = go(alone, ["--config", cfg, "--unit", "bl-en"] + terminal, "1")
    ran_terminal = "Could not start the Braille Lite 2000 (English)" in out
    check("frontend missing: one line, the terminal", MISSING in out and ran_terminal and FOUND not in out,
          "said %s, terminal emulator %s" % ("so" if MISSING in out else "nothing",
                                             "ran" if ran_terminal else "did not run"))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    emu = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="bt-handover-") as tmp:
        detection(emu, tmp)
        hand_over(emu, tmp)
    if failures:
        print("bt hand-over: %d of %d FAILED" % (failures, ran))
        return 1
    print("bt hand-over: all %d passed" % ran)
    return 0


if __name__ == "__main__":
    sys.exit(main())
