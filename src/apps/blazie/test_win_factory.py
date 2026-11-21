"""The Windows emulator's "Back to the factory state" after a unit failed to start (issue #38).

The app remembered a unit as its own only once it had started.  If the unit chosen at start (or switched to) failed --
its firmware missing, its saved memory damaged -- the app still held the one before (the English Braille Lite at
start), and Back to the factory state, the natural remedy, erased THAT unit's memory and its firmware update, started
it, and left the failed one as it was.  Its question named no unit, and the title stayed "Starting".

The real program, a private copy in a scratch folder (its own blazie_emu.ini beside it, its own APPDATA: nothing of
yours is read or written), driven by window messages only -- no keystrokes:
  failed      chosen at start: the Spanish unit, its memory damaged; the English one's saved memory good.  It does not
              start (its message is answered OK), and the title no longer says "Starting"
  question    Back to the factory state names the unit it will put back: the Spanish one
  kept        answered Yes: the English unit's memory is as it was, byte for byte
  reset       the Spanish unit now runs from its factory state, and is the one the settings name

    python test_win_factory.py <blazie_emu.exe> <firmware folder: BL2ENG.BNS, bl2_2003_warm.state, BL2SPA.BNS,
                                bl2spa_fresh.state>                      exit 1 on any failure; Windows only
    EMU_FACTORY_BREAK=1   the app keeps the unit before as its own after a failed start   -- must fail: kept, reset
"""
import ctypes
import ctypes.wintypes as wt
import os
import shutil
import subprocess
import sys
import tempfile
import time

ID_FACTORY = 108                      # main_win.c: ID_EN = 100 ... ID_IT, ID_FACTORY
WM_COMMAND, WM_CLOSE, BM_CLICK = 0x0111, 0x0010, 0x00F5
IDOK, IDCANCEL, IDYES, IDNO = 1, 2, 6, 7   # (an MB_OK box's one button is IDCANCEL's, so Esc closes it)
u32 = ctypes.windll.user32
EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
bad = 0


def check(name, ok, detail):
    global bad
    print("%-4s %-9s %s" % ("ok" if ok else "FAIL", name, detail))
    bad += not ok


def windows_of(pid):
    out = []

    def each(h, _l):
        p = wt.DWORD()
        u32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u32.IsWindowVisible(h):
            out.append(h)
        return True
    u32.EnumWindows(EnumProc(each), 0)
    return out


def text(h):
    n = u32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def class_of(h):
    b = ctypes.create_unicode_buffer(64)
    u32.GetClassNameW(h, b, 64)
    return b.value


def dialog(pid, timeout=20.0):
    """the process's message box (#32770) and its message, or (None, "")"""
    t0 = time.monotonic()
    while time.monotonic() - t0 < timeout:
        for h in windows_of(pid):
            if class_of(h) == "#32770":
                msg = u32.GetDlgItem(h, 0xFFFF)          # a message box's text
                return h, text(msg) if msg else ""
        time.sleep(0.1)
    return None, ""


def answer(pid, says, button, timeout=20.0):
    """the message box whose text has `says`: its text, after `button` was clicked and it has closed; every other
    message seen meanwhile is answered and listed in `others`"""
    t0 = time.monotonic()
    while time.monotonic() - t0 < timeout:
        box, msg = dialog(pid, timeout=0.5)
        if box is None or box in answered:
            time.sleep(0.05)
            continue
        answered.add(box)
        mine = says in msg
        want = button if mine else IDCANCEL
        b = u32.GetDlgItem(box, want) or u32.GetDlgItem(box, IDCANCEL if want == IDOK else IDNO)
        if not b:
            print("      a message box with no button to answer: %r" % msg[:80])
            return None
        u32.SendMessageW(b, BM_CLICK, 0, 0)
        t1 = time.monotonic()
        while u32.IsWindow(box) and time.monotonic() - t1 < 5.0:
            time.sleep(0.05)
        if mine:
            return msg
        others.append((box, msg))
    return None


others = []
answered = set()                      # boxes clicked: one seen again before it closed is not another


def main_window(pid, timeout=20.0):
    t0 = time.monotonic()
    while time.monotonic() - t0 < timeout:
        for h in windows_of(pid):
            if class_of(h) == "BlazieEmulator":
                return h
        time.sleep(0.1)
    return None


def main():
    exe, fw = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    tmp = tempfile.mkdtemp(prefix="emu_fr_test_")
    proc = None
    try:
        app = os.path.join(tmp, "app")
        os.makedirs(app)
        shutil.copy(exe, app)
        with open(os.path.join(app, "blazie_emu.ini"), "w") as f:
            f.write("[unit]\nkind=spanish\nfirmware_dir=%s\n" % fw)
        saves = os.path.join(tmp, "appdata", "ssi263-speech", "blazie-emu")
        os.makedirs(saves)
        shutil.copy(os.path.join(fw, "bl2_2003_warm.state"), os.path.join(saves, "english.state"))
        english = open(os.path.join(saves, "english.state"), "rb").read()
        with open(os.path.join(saves, "spanish.state"), "wb") as f:
            f.write(b"\x55" * 100)                           # damaged: no state is 100 bytes
        env = dict(os.environ, APPDATA=os.path.join(tmp, "appdata"))
        proc = subprocess.Popen([os.path.join(app, "blazie_emu.exe")], env=env)
        msg = answer(proc.pid, "Could not start the Braille Lite 2000 (Spanish)", IDOK)
        w = main_window(proc.pid)
        time.sleep(1.0)
        title = text(w) if w else ""
        check("failed", msg is not None and w is not None and "Starting" not in title,
              "the damaged Spanish unit: %s; the title then %r" % ("its message shown" if msg else "NO such message",
                                                                   title))
        u32.PostMessageW(w, WM_COMMAND, ID_FACTORY, 0)
        msg = answer(proc.pid, "as it left the factory", IDYES)
        check("question", msg is not None and "Spanish" in msg, "Back to the factory state asks: %r" % (msg or "")[:140])
        time.sleep(4.0)                                      # the factory state started
        answer(proc.pid, "\x00(none expected)", IDOK, timeout=1.0)
        if others:
            print("      other messages: %s" % " | ".join(repr(m[:100]) for _b, m in others))
        u32.PostMessageW(w, WM_CLOSE, 0, 0)
        proc.wait(timeout=20)
        proc = None
        path = os.path.join(saves, "english.state")
        kept = os.path.isfile(path) and open(path, "rb").read() == english
        check("kept", kept, "the English unit's memory: %s" % ("as it was" if kept else "GONE" if not os.path.isfile(path)
                                                               else "CHANGED"))
        ini = open(os.path.join(app, "blazie_emu.ini")).read()
        sp = os.path.join(saves, "spanish.state")
        spanish_ok = os.path.isfile(sp) and os.path.getsize(sp) > 100000
        check("reset", spanish_ok and "kind=spanish" in ini, "the Spanish unit's memory after: %s; the settings name %s" % (
            "%d bytes" % os.path.getsize(sp) if os.path.isfile(sp) else "none",
            [l for l in ini.splitlines() if l.startswith("kind=")]))
    finally:
        if proc is not None:
            proc.kill()
        shutil.rmtree(tmp, ignore_errors=True)
    print("factory after a failed start: %s" % ("%d FAILED" % bad if bad else "all ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
