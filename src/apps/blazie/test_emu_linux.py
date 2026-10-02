"""The Linux emulator (blazie_emu), headless: the whole program -- its key decoding, its units, its memory folder --
run with no sound card and no terminal (--null), keys typed from a script at their times, as a terminal would send
them.

    python3 src/apps/blazie/test_emu_linux.py BLAZIE_EMU FIRMWARE_DIR [--only NAME,NAME]

FIRMWARE_DIR holds BL2ENG.BNS and bl2_2003_warm.state, and the Type 'n Speak's TNSENG.TNS (in tns/ or beside them).
The checks:
  boot           the Braille Lite boots and speaks; F (dot 1) typed at 8 s is answered, and not without it
  clock-keys     o-chord t typed as keys (f s k space, then d s j k): the unit says the time, the host's
  clock-letters  the same in letters mode (O, then t: the BTSpeak's computer braille, a capital as its chord)
  tns            the Type 'n Speak from cold: it speaks, its cold reset's seven questions answered y, then F4 says
                 the host's time
  memory         the date and time set through the unit's own commands (typed as keys), the memory saved to the
                 program's folder (--autosave) and started from again: the unit still holds 2015-09-30
  serial         the program run as a person runs it (in a pseudo-terminal, no sound card), its serial port on a
                 pseudo-terminal: s-chord's XON ENQ arrive at 19200 bit/s, ACK is answered with 'C' -- and NAK
                 (the control) is not
  held           an input device's keys: p-chord, l, i-chord held through the restart -- the cold reset's question
  buffer         the program in a pseudo-terminal: menu 17, the sound buffer (Tomi: the emulator's speech stutters),
                 chosen long, written to the settings as the Windows app writes it ([sound] buffer = long), and
                 shown so when the program starts again
  btspeak        the program in a pseudo-terminal with a BT Speak keyboard server of the test's own (the device's
                 protocol: btkb_linux.h): it takes the keyboard exclusively and consumes every key; dot 1 down and up
                 is the unit's chord; dot 8 tapped is the advance bar, down and up; R3 the advance bar too and L2
                 the back bar (a BT Braille's); M-chord with dot 7 opens the menu and gives the keyboard back, Enter
                 takes it again, and Z-chord with dot 7 saves and ends the program
The control: BLAZIE_KEYS_BREAK=1 swaps dots 1 and 4 in every chord; clock-keys and clock-letters must then FAIL
(tools/linux_tests.sh judges it by its marks).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

failures = 0
ran = 0


def check(name, ok, detail):
    global failures, ran
    print("%-4s %-34s %s" % ("ok" if ok else "FAIL", name, detail))
    failures += not ok
    ran += 1


def run(exe, args, script=None, tmp=None):
    """blazie_emu's output; script: lines "T type TEXT" / "T down KEY" / "T up KEY"."""
    cmd = [exe] + args
    if script is not None:
        path = os.path.join(tmp, "script.txt")
        with open(path, "w") as f:
            f.write("\n".join(script) + "\n")
        cmd += ["--script", path]
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True, timeout=600)
    return p.stdout


def rms(out, frm, to):
    m = re.search(r"^rms %.2f-%.2f s: ([0-9.]+)$" % (frm, to), out, re.M)
    return float(m.group(1)) if m else -1.0


def said_time(out):
    m = re.search(r"^clock: (.*)$", out, re.M)
    return (m is not None and m.group(1).endswith(": yes")), (m.group(1) if m else "no clock line")


def serial_handshake(exe, fw, cfg, reply):
    """The program as a person runs it (a terminal: a pseudo-terminal here; --no-sound, so the system clock paces
    it), its serial port on a pseudo-terminal from its settings; s-chord typed; this script, on the line's other
    end, answers the unit's ENQ with `reply` at once (test_serial.c's handshake).  (bytes up to the ENQ, bytes after
    the reply, the line's speed then)."""
    import pty
    import select
    import termios
    import time
    import tty
    os.makedirs(cfg)
    with open(os.path.join(cfg, "blazie_emu.ini"), "w") as f:
        f.write("[input]\nevdev = off\nbt = off\n\n[serial]\nport = pty\n")
    pid, fd = pty.fork()
    if pid == 0:
        os.execv(exe, [exe, "--firmware", fw, "--config", cfg, "--unit", "bl-en", "--no-sound"])
    first, after, speed, ser = bytearray(), bytearray(), "?", None
    speeds = {getattr(termios, "B%d" % b): str(b) for b in (9600, 19200)}

    def pump(sec, into=None, until=None):
        end = time.time() + sec
        while time.time() < end and not (until and until in into):
            r, _, _ = select.select([fd] + ([ser] if ser is not None else []), [], [], 0.002)
            if fd in r:
                try:
                    os.read(fd, 4096)
                except OSError:
                    return
            if ser is not None and ser in r:
                try:
                    into.extend(os.read(ser, 256))
                except OSError:                 # EIO: the program closed its pseudo-terminal as it exited (F11, 0)
                    return
    try:
        pump(3.0)
        ser = os.open(os.path.join(cfg, "serial"), os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(ser)
        pump(5.0, first)                        # the greeting over
        os.write(fd, b"dsj ")                   # s-chord: storage
        pump(3.0, first, b"\x05")
        os.write(ser, reply)
        pump(2.0, after)
        speed = speeds.get(termios.tcgetattr(ser)[4], "other")
        os.write(fd, b"\x1b[23~")               # F11, then 0: exit
        pump(1.0, after)
        os.write(fd, b"0\n")
        pump(3.0, after)
    except OSError as e:
        speed = "error: %s" % e
    finally:
        try:
            os.kill(pid, 15)
            os.waitpid(pid, 0)
        except OSError:
            pass
        if ser is not None:
            os.close(ser)
    return bytes(first), bytes(after), speed


def menu_session(exe, fw, cfg, typed):
    """The program in a pseudo-terminal (--no-sound), F11 opening its menu, then each of `typed` (a line) in turn;
    what it said."""
    import pty
    import select
    import time
    if not os.path.isdir(cfg):
        os.makedirs(cfg)
        with open(os.path.join(cfg, "blazie_emu.ini"), "w") as f:
            f.write("[input]\nevdev = off\nbt = off\n")
    pid, fd = pty.fork()
    if pid == 0:
        os.execv(exe, [exe, "--firmware", fw, "--config", cfg, "--unit", "bl-en", "--no-sound"])
    said = bytearray()

    def pump(sec):
        end = time.time() + sec
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.02)
            if fd in r:
                try:
                    said.extend(os.read(fd, 4096))
                except OSError:                 # EIO: the program has exited
                    return
    try:
        pump(3.0)
        os.write(fd, b"\x1b[23~")              # F11
        pump(1.0)
        for line in typed:
            os.write(fd, line.encode() + b"\n")
            pump(1.0)
        pump(2.0)
    finally:
        try:
            os.kill(pid, 15)
            os.waitpid(pid, 0)
        except OSError:
            pass
    return said.decode("utf-8", "replace").replace("\r", "")


def btspeak_session(exe, fw, cfg):
    """The program in a pseudo-terminal (--no-sound, --print-actions) with a keyboard server of this test's own: what
    it said, the hellos the server got, and each key's answer."""
    import pty
    import select
    import socket
    import struct
    import threading
    import time
    os.makedirs(cfg, exist_ok=True)
    with open(os.path.join(cfg, "blazie_emu.ini"), "w") as f:
        f.write("[input]\nevdev = off\nbt = native\n\n[btspeak]\ndisplay = off\n")   # never a real BRLTTY's display,
                                                    # nor the BT front end (it would take a BT device's real keyboard)
    path = os.path.join(cfg, "keyboard-socket")
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(path)
    srv.listen(2)
    hellos, acks, conns = [], [], []

    def accept():
        while True:
            try:
                c, _ = srv.accept()
            except OSError:
                return
            hellos.append(c.recv(8))
            c.sendall(b"BTKB\x01\x00\x00\x00")
            conns.append(c)
    threading.Thread(target=accept, daemon=True).start()

    def key(code, down):
        c = conns[-1]
        c.sendall(struct.pack("<BHB", 1, code, down))
        c.settimeout(1.0)
        acks.append(c.recv(2))
    env = dict(os.environ, BLAZIE_BTKB_SOCKET=path)
    pid, fd = pty.fork()
    if pid == 0:
        os.execve(exe, [exe, "--firmware", fw, "--config", cfg, "--unit", "bl-en", "--no-sound", "--print-actions"],
                  env)
    said = bytearray()

    def pump(sec):
        end = time.time() + sec
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.02)
            if fd in r:
                try:
                    said.extend(os.read(fd, 4096))
                except OSError:
                    return
    gone_after_menu = False
    ended = False
    try:
        pump(3.0)
        SPACE, DOT7, DOT8, L2, R3 = 57, 0x1F7, 0x1F8, 0x101, 0x105
        DOT = {n: 0x1F0 + n for n in range(1, 9)}

        def chord(first, rest):                # `first` down, then the rest; all up, the last of them last
            key(first, 1); pump(0.02)
            for c in rest:
                key(c, 1)
            pump(0.1)
            for c in [first] + rest:
                key(c, 0)
        if conns:
            key(DOT[1], 1); pump(0.05); key(DOT[1], 0); pump(0.5)
            key(DOT8, 1); pump(0.03); key(DOT8, 0); pump(0.5)
            key(R3, 1); pump(0.2); key(R3, 0); pump(0.5)
            key(L2, 1); pump(0.2); key(L2, 0); pump(0.5)
            chord(DOT7, [SPACE, DOT[1], DOT[3], DOT[4]])   # M-chord with dot 7: the menu
            pump(1.5)
            conns[-1].settimeout(1.0)
            try:
                gone_after_menu = conns[-1].recv(1) == b""
            except OSError:
                gone_after_menu = False
            os.write(fd, b"\n")                # Enter alone: back to the unit
            pump(1.5)
            if len(conns) > 1:                 # Z-chord with dot 7: saved, and the program ends of itself
                chord(SPACE, [DOT[1], DOT[3], DOT[5], DOT[6], DOT7])
                pump(3.0)
                ended = os.waitpid(pid, os.WNOHANG)[0] == pid
    finally:
        if not ended:
            try:
                os.kill(pid, 15)
                os.waitpid(pid, 0)
            except OSError:
                pass
        srv.close()
    return said.decode("utf-8", "replace").replace("\r", ""), hellos, acks, gone_after_menu, ended


def main():
    exe, fw = sys.argv[1], sys.argv[2]
    only = sys.argv[sys.argv.index("--only") + 1].split(",") if "--only" in sys.argv else None
    tns = next((p for p in (os.path.join(fw, "tns", "TNSENG.TNS"), os.path.join(fw, "TNSENG.TNS"))
                if os.path.isfile(p)), None)
    tmp = tempfile.mkdtemp(prefix="blazie_emu_test.")
    base = ["--firmware", fw, "--null", "--rate", "22050"]
    try:
        def want(name):
            return only is None or name in only

        if want("boot"):
            a = run(exe, base + ["--unit", "bl-en", "--seconds", "10", "--rms", "0:6", "--rms", "8.2:10"],
                    ["8.0 type f"], tmp)
            b = run(exe, base + ["--unit", "bl-en", "--seconds", "10", "--rms", "8.2:10"], [], tmp)
            g, w, wo = rms(a, 0, 6), rms(a, 8.2, 10), rms(b, 8.2, 10)
            check("boot greeting", g > 0.01, "rms %.4f over the first 6 s" % g)
            check("a chord answered (F typed)", w > 0.01 and 0 <= wo < 0.002,
                  "8.2-10 s: rms %.4f with F typed at 8 s, %.4f without (the control)" % (w, wo))

        if want("clock-keys"):
            out = run(exe, base + ["--unit", "bl-en", "--seconds", "16", "--clock-check"],
                      ["12.0 type fsk ", "13.5 type dsjk"], tmp)
            ok, d = said_time(out)
            check("the clock, from the system time (keys)", ok, d)

        if want("clock-letters"):
            out = run(exe, base + ["--unit", "bl-en", "--input", "letters", "--seconds", "16", "--clock-check"],
                      ["12.0 type O", "13.5 type t"], tmp)
            ok, d = said_time(out)
            check("the clock, from the system time (letters)", ok, d)

        if want("tns"):
            if not tns:
                check("Type 'n Speak boot", False, "no TNSENG.TNS in %s (or its tns folder)" % fw)
            else:
                # its cold reset's seven questions, y each (tns_setup.c's times; the last starts a 35 s wipe), then F4
                answers = ["%.1f type y" % t for t in (4.0, 7.0, 10.0, 13.0, 18.0, 25.0, 28.0)]
                out = run(exe, base + ["--unit", "tns-en", "--flash-instant", "--seconds", "76", "--rms", "0:6",
                                       "--clock-check"],
                          answers + ["72.0 type \\eOS"], tmp)
                g = rms(out, 0, 6)
                ok, d = said_time(out)
                check("Type 'n Speak boot (from cold)", g > 0.01 and "from cold" in out,
                      "rms %.4f over the first 6 s, %s" % (g, "a cold start" if "from cold" in out else "NOT cold"))
                check("Type 'n Speak: its setup answered y, then F4 the time", ok, d)

        if want("memory"):
            cfg = os.path.join(tmp, "config")
            # o-chord s d 0 9 3 0 1 5, o-chord s t 1 2 3 4 p -- test_clock.c's commands, typed as keys 0.4 s apart
            o, s, d, t = "fsk ", "dsj", "fjk", "dsjk"
            digits = {"0": "skl", "9": "sk", "3": "dk", "1": "d", "5": "dl", "2": "ds", "4": "dkl"}
            lines = []
            for at, word in ((18.0, [o, s, d]), (21.0, [digits[c] for c in "093015"]), (28.0, [o, s, t]),
                             (31.0, [digits[c] for c in "1234"]), (34.0, ["fdsj"])):
                lines += ["%.1f type %s" % (at + 0.4 * i, k) for i, k in enumerate(word)]
            one = run(exe, base + ["--config", cfg, "--autosave", "--unit", "bl-en", "--seconds", "38",
                                   "--clock-check"], lines, tmp)
            path = os.path.join(cfg, "english.state")
            saved = os.path.isfile(path) and os.path.getsize(path) >= 786432
            m = re.search(r"the unit's clock (\d{4}-\d\d-\d\d)", one)
            check("memory: set through the unit, saved", saved and m is not None and m.group(1) == "2015-09-30",
                  "the clock %s; %s %s" % (m.group(1) if m else "?", path if saved else "NOT saved",
                                           "(%d bytes)" % os.path.getsize(path) if saved else ""))
            two = run(exe, base + ["--config", cfg, "--autosave", "--unit", "bl-en", "--seconds", "4",
                                   "--rms", "0:4", "--clock-check"], [], tmp)
            m = re.search(r"the unit's clock (\d{4}-\d\d-\d\d)", two)
            started = "starting bl-en from %s" % path in two
            g = rms(two, 0, 4)
            check("memory: started again from it", started and m is not None and m.group(1) == "2015-09-30"
                  and g > 0.01, "%s; the clock %s; rms %.4f" % ("from the saved memory" if started else
                                                               "NOT from the saved memory", m.group(1) if m else "?", g))

        if want("serial"):
            ack = serial_handshake(exe, fw, os.path.join(tmp, "serial-ack"), b"\x06")
            nak = serial_handshake(exe, fw, os.path.join(tmp, "serial-nak"), b"\x15")
            check("serial port on a pseudo-terminal", ack[0].endswith(b"\x11\x05") and b"C" in ack[1]
                  and ack[2] == "19200" and nak[0].endswith(b"\x11\x05") and b"C" not in nak[1],
                  "s-chord sent [%s] at %s bit/s; ACK answered [%s], NAK (the control) [%s]"
                  % (ack[0].hex(" "), ack[2], ack[1].hex(" "), nak[1].hex(" ")))

        if want("held"):
            # an input device (evdev): p-chord, l, then i-chord (dots 2 4 and space) held while the unit restarts
            lines = []
            def chord(at, keys, hold=0.05):
                for i, k in enumerate(keys):
                    lines.append("%.3f down %s" % (at + 0.01 * i, k))
                for i, k in enumerate(keys):
                    lines.append("%.3f up %s" % (at + 0.01 * len(keys) + hold + 0.01 * i, k))
            chord(8.0, ["f", "d", "s", "j", "space"])
            chord(10.0, ["f", "d", "s"])
            chord(10.25, ["d", "j", "space"], hold=1.75)
            lines.sort(key=lambda x: float(x.split()[0]))
            out = run(exe, base + ["--unit", "bl-en", "--seconds", "14", "--ram-has", "initialize file system"],
                      lines, tmp)
            ok = 'ram has "initialize file system": yes' in out
            check("i-chord held through the restart", ok, "the unit asked \"initialize file system\": %s"
                  % ("yes" if ok else "no"))

        if want("btspeak"):
            out, hellos, acks, gone, ended = btspeak_session(exe, fw, os.path.join(tmp, "btspeak"))
            actions = re.findall(r"^[0-9.]+ (chord|held) (0x[0-9A-F]+)", out, re.M)
            check("BT keyboard: taken exclusively", len(hellos) >= 1 and hellos[0] == b"BTKB\x01\x03\x00\x00"
                  and "Keys: the BT keyboard" in out, "hellos %r" % hellos)
            check("BT keyboard: every key consumed", len(acks) == 30 and all(a == b"\x20\x00" for a in acks),
                  "%d answers: %s" % (len(acks), " ".join(a.hex() for a in acks)))
            seq = " ".join("%s %s" % a for a in actions)
            check("BT keyboard: dot 1, dot 8, R3 (advance) and L2 (back)", seq.startswith(
                "held 0x01 held 0x00 chord 0x01 held 0x80 held 0x00 held 0x80 held 0x00 held 0x100 held 0x00"),
                "the unit got [%s]" % seq)
            check("BT keyboard: Z-chord with dot 7 saves and ends the program", ended
                  and "Leaving: the unit's memory is saved." in out, "ended of itself: %s" % ("yes" if ended else "no"))
            check("BT keyboard: M-chord with dot 7 opens the menu, gives the keyboard back, Enter takes it again",
                  re.search(r"^ +1 ", out, re.M) is not None and gone and len(hellos) == 2 and "Back to the" in out,
                  "menu listed: %s; keyboard given back: %s; taken again: %s" % (
                      "yes" if re.search(r"^ +1 ", out, re.M) else "no", "yes" if gone else "no",
                      "yes" if len(hellos) == 2 else "no"))

        if want("buffer"):
            cfg = os.path.join(tmp, "buffer")
            first = menu_session(exe, fw, cfg, ["17", "3", "0"])     # the sound buffer: long (3: no short); exit
            ini = open(os.path.join(cfg, "blazie_emu.ini")).read() if os.path.isfile(
                os.path.join(cfg, "blazie_emu.ini")) else ""
            again = menu_session(exe, fw, cfg, ["0"])
            listed = re.search(r"^ +17 Sound buffer: (.*)$", first, re.M)
            relisted = re.search(r"^ +17 Sound buffer: (.*)$", again, re.M)
            check("menu 17: the sound buffer", listed is not None and listed.group(1) == "automatic"
                  and "Sound buffer: long (250 ms" in first and re.search(r"^buffer = long$", ini, re.M) is not None
                  and relisted is not None and relisted.group(1) == "long (250 ms)",
                  "listed %r; chosen long: %s; [sound] buffer = long in the settings: %s; listed again %r" % (
                      listed.group(1) if listed else None, "said" if "Sound buffer: long (250 ms" in first
                      else "NOT said", "yes" if re.search(r"^buffer = long$", ini, re.M) else "no",
                      relisted.group(1) if relisted else None))
            # no short among the choices: 40 and 50 ms both broke up on a ROG Ally (audio_pace.h AP_OFFERED)
            check("menu 17: no short sound buffer offered", re.search(r"^ +\d+ short", first, re.M | re.I) is None,
                  "the choices: %s" % ", ".join(re.findall(r"^ +\d+ ((?:automatic|short|medium|long)\b[^(]*)",
                                                           first, re.M | re.I)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if failures:
        print("emulator: %d of %d FAILED" % (failures, ran))
    else:
        print("emulator: all %d passed" % ran)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
