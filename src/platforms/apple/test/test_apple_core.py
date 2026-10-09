"""The Apple apps' native core, proven on the Mac: SSI263Core's own slices (build_apple.sh) speak as the desktop.

Android's host-side program (src/platforms/android/test/test_android_native.c: the front end ssa_engine, ssa_map and
ssa_import over every voice) is linked against each runnable slice's libssi263core.a, the library the apps link, and
run on the same firmware as test_android_native.py's desktop program; every case's samples and PCM hash must be the
desktop's, and so must the Accent SA's front-end text.  test_android_native.py holds that desktop program to the
references (bl_voice as sd_ssi263 drives it, so_voice, am_voice, the NVDA Accent driver), so the slices are held to
them too.

    macos-arm64      run natively
    macos-x86_64     run under Rosetta (skipped, and said so, without it)
    iossim-arm64     with --simulator: run in a booted iOS simulator (xcrun simctl spawn); one is booted for the run
                     and shut down afterwards when none is
    ios-arm64        a device: not here (the on-device check is the app's)

    sh src/platforms/apple/build_apple.sh                       first
    python src/platforms/apple/test/test_apple_core.py [--simulator]
    SSI263_APPLE_TEST_BREAK=1 python ...                        the control: the slices run with the request's rate
                                                                dropped (ssa_map_break), so this run must FAIL

SSI263_ANDROID_TEST_ONLY chooses the blocks on both sides, as in test_android_native.py.  Options: --firmware
<folder> (default $SSI263_FIRMWARE, else firmware/blazie), --aicom <folder> (default firmware/aicom-accent-sa).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
ANDROID_TEST = os.path.join(REPO, "src", "platforms", "android", "test")
sys.path.insert(0, ANDROID_TEST)
import test_android_native as tan  # noqa: E402

APPLE = os.path.join(REPO, "build", "apple")
OUT = os.path.join(APPLE, "test")
MACOS_MIN, IOS_MIN = "13.0", "16.0"       # build_apple.sh's
SLICES = {
    "macos-arm64": ("macosx", "arm64-apple-macos" + MACOS_MIN),
    "macos-x86_64": ("macosx", "x86_64-apple-macos" + MACOS_MIN),
    "iossim-arm64": ("iphonesimulator", "arm64-apple-ios%s-simulator" % IOS_MIN),
}


def xcrun(*args):
    return subprocess.run(["xcrun"] + list(args), capture_output=True, text=True, check=True).stdout.strip()


def build_slice_program(name):
    """test_android_native.c, compiled for the slice and linked against its libssi263core.a."""
    sdk, target = SLICES[name]
    lib = os.path.join(APPLE, "slices", name, "libssi263core.a")
    if not os.path.isfile(lib):
        sys.exit("no %s: run sh src/platforms/apple/build_apple.sh first" % os.path.relpath(lib, REPO))
    os.makedirs(OUT, exist_ok=True)
    exe = os.path.join(OUT, "test_android_native-" + name)
    sysroot = xcrun("--sdk", sdk, "--show-sdk-path")
    common = ["--target=" + target, "-isysroot", sysroot]
    obj = exe + ".o"
    subprocess.run([xcrun("--sdk", sdk, "--find", "clang")] + common +
                   ["-O2", "-std=c99", "-ffp-contract=off", "-Wall", "-I" + tan.SRC, "-I" + tan.CPP, "-c", "-o", obj,
                    os.path.join(ANDROID_TEST, "test_android_native.c")], check=True)
    subprocess.run([xcrun("--sdk", sdk, "--find", "clang++")] + common + ["-o", exe, obj, lib, "-lm"], check=True)
    os.remove(obj)
    return exe


class Simulator:
    """A booted iOS simulator for the run: the one already booted, or one booted here and shut down afterwards."""

    def __init__(self):
        self.udid, self.ours = None, False

    def __enter__(self):
        import json
        devices = json.loads(xcrun("simctl", "list", "devices", "available", "--json"))["devices"]
        ios = [d for rt, ds in devices.items() if "iOS" in rt for d in ds]
        booted = [d for d in ios if d["state"] == "Booted"]
        if booted:
            self.udid = booted[0]["udid"]
        else:
            phones = [d for d in ios if d["name"].startswith("iPhone")] or ios
            if not phones:
                sys.exit("no iOS simulator is installed")
            self.udid, self.ours = phones[-1]["udid"], True
            subprocess.run(["xcrun", "simctl", "bootstatus", self.udid, "-b"], capture_output=True, check=True)
        return self

    def __exit__(self, *exc):
        if self.ours:
            subprocess.run(["xcrun", "simctl", "shutdown", self.udid], capture_output=True)

    def prefix(self):
        return ["xcrun", "simctl", "spawn", self.udid]


def run(prefix, exe, args, env, stdin=None):
    r = subprocess.run(prefix + [exe] + args, input=stdin, capture_output=True, text=True, env=env)
    if r.returncode:
        sys.exit("%s failed (%d): %s" % (os.path.basename(exe), r.returncode, r.stderr.strip()[-2000:]))
    return r.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--firmware", default=os.environ.get("SSI263_FIRMWARE") or os.path.join(REPO, "firmware", "blazie"))
    ap.add_argument("--aicom", default=tan.AICOM)
    ap.add_argument("--simulator", action="store_true")
    a = ap.parse_args()
    # the desktop side never runs broken: only the slices do, under the control
    breaking = os.environ.pop("SSI263_APPLE_TEST_BREAK", "")
    os.environ.pop("SSI263_ANDROID_TEST_BREAK", None)
    slice_env = dict(os.environ)
    if breaking:
        slice_env["SSI263_ANDROID_TEST_BREAK"] = breaking
    # simctl spawn hands the child the caller's variables prefixed SIMCTL_CHILD_
    sim_env = dict(slice_env)
    if breaking:
        sim_env["SIMCTL_CHILD_SSI263_ANDROID_TEST_BREAK"] = breaking
    for k in ("SSI263_ANDROID_TEST_ONLY",):
        if k in os.environ:
            sim_env["SIMCTL_CHILD_" + k] = os.environ[k]
    mini = [tan.MINI_DVC] if os.path.isfile(tan.MINI_DVC) else []
    texts_in = "".join(t.encode("utf-8").hex() + "\n" for t in tan.TEXTS)
    tmp = tempfile.mkdtemp(prefix="ssi263-apple-test-")
    bad = 0
    try:
        data = tan.data_folder(a.firmware, tmp)
        desktop = tan.build_desktop()
        want = tan.parse(run([], desktop, [data, a.aicom] + mini, dict(os.environ)))
        want_texts = run([], desktop, ["--texts"], dict(os.environ), texts_in) if tan.block("accent") else None
        print("desktop  %d cases (build/android-host/test_android_native, held to the references by "
              "test_android_native.py)" % len(want))
        runs = [("macos-arm64", [], slice_env)]
        try:
            subprocess.run(["arch", "-x86_64", "/usr/bin/true"], check=True, capture_output=True)
            runs.append(("macos-x86_64", ["arch", "-x86_64"], slice_env))
        except (OSError, subprocess.CalledProcessError):
            print("skip  macos-x86_64: Rosetta is not installed")
        sim = Simulator() if a.simulator else None
        if sim:
            sim.__enter__()
            runs.append(("iossim-arm64", sim.prefix(), sim_env))
        try:
            for name, prefix, env in runs:
                exe = build_slice_program(name)
                got = tan.parse(run(prefix, exe, [data, a.aicom] + mini, env))
                differ = sorted(k for k in set(want) | set(got) if want.get(k) != got.get(k))
                for k in differ:
                    print("FAIL  %-13s %s: %s, desktop %s" % (name, k, got.get(k), want.get(k)))
                if want_texts is not None and run(prefix, exe, ["--texts"], env, texts_in) != want_texts:
                    print("FAIL  %-13s the Accent SA's front-end text differs" % name)
                    differ.append("texts")
                print("%-5s %-13s %d of %d cases as the desktop, byte for byte%s" % (
                    "ok" if not differ else "FAIL", name, len(want) - len([k for k in differ if k in want]),
                    len(want), "" if want_texts is None else ", and the texts"))
                bad += len(differ)
        finally:
            if sim:
                sim.__exit__(None, None, None)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("FAILED: %d difference(s)" % bad if bad else "SSI263Core speaks as the desktop, byte for byte")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
