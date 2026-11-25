"""Run ahead against the lockstep, as FIRMWARE AND BOARD STATE at semantic checkpoints, then under the same continuation
(Astra, Reply 107).  Through bl.dll with the driver's boot.

A matching register prefix does not establish future equivalence, and the two machines' clocks cannot match cycle
for cycle: run ahead runs the unit ahead of the chip and then holds its CPU while the script plays.  So the units are
compared where they mean the same thing -- the checkpoints -- and given the same continuation afterwards:

  boot         the driver's boot, before any say (both lockstep: a sanity check)
  said         a two-line say, done, then 0.5 s with no input
  setting      a rate setting sent, then 0.5 s
  faster       a say at the new rate, done, 0.5 s
  cancelled    a three-line say cancelled 0.5 s in (the unit run ahead has read further when ^X reaches it)
  respoken     a say at once after the cancel, done, 0.5 s
  clicked off  12 s with no input (the firmware clicks the channel off ~10 s after speech: port A0 bit 1, R3 = 00)
  after        a last say, done, 0.5 s

At each: the RAM (40000h-FFFFFh, its length too) and the file flash; the serial bytes the unit sent during the step
(^F echoes, XON/XOFF); queued serial input, a ^X in flight, the unit's XOFF; the braille-key latch; the channel power
(port A0 bit 1) and port E0; the SSI-263 registers as the unit wrote them and as the chip holds them; A/R as the unit
sees it and /INT1 (the pending speech interrupt), /INT2, IFF1/2, IM, HALT/SLP; the ASCI's registers; the host's ^F
accounting (sent, echoed, stale, owed) and input held.  Then every step's write values.  A checkpoint that never
reaches the idle loop (asleep, SP D3FE) within 50 ms fails; so do memories of different lengths.

INTENTIONAL differences, listed, not gated: the CPU's cycle count (run ahead's unit ran ahead, then waited); per
checkpoint, the RAM cells and probe fields that also differ there between LOCKSTEP runs whose only difference is
timing (steps 0.125-0.5 ms, 0.06-0.2 s more idle) -- never a union over the session, which let a difference at one
checkpoint hide one at another; the stack below SP; and NAMED cells, each from a trace build's watchpoints (the
routine that writes it, and when):
  every checkpoint    CLOCK: the firmware's seconds counters (its timer interrupt counts CPU time)
  from 'cancelled'    RESIDUE: what the unit read ahead of the listener before ^X -- the line-staging buffer, the
                      banked-call save slots
  at 'cancelled'      the unit's serial output, ^F accounting and chip request (the same registers: the pause after
                      ^X, its timer), write values up to the cut (a cancel-prefix match); its text index
  after it            a redundant XON (the flow state the same)
Anything else that differs is a FINDING and fails.

RESULTS (2026-09-30).  Without the cancel, and with it (a three-line say cancelled 0.5 s in, then a say at once),
English and Spanish: every checkpoint agrees under those names, and every write value after the cancel.  Before the
fix the English respoken utterance began with phonemes of the cancelled text (writes differ at 12 of 175/181, its
text buffer shifted by one: a character of the cancelled line survived ^X); the control puts that back.  The
mechanism and its sweeps: run_ahead_cancel.py.  The stack page is inferred from where timing alone differs (below
SP), not read from the MMU: cpu.h exposes no MMU registers.

    python run_ahead_state.py [--es] [--no-cancel]
    RUN_AHEAD_STATE_BREAK=1: one RAM byte changed (must fail); =settle: run_ahead.h's RA_BRK_SETTLE (must fail)
The internal timers and interrupt flags of the Z180 itself are not exposed (src/csrc/cpu/cpu.h): not compared.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path[:0] = [REPO, os.path.join(REPO, "src")]
from ssi263.native import SSI263C                 # noqa: E402
from hosts.native_blazie import NativeBlazie       # noqa: E402

ENG = os.path.join(REPO, "nvda", "dist", "blazie-build", "synthDrivers", "_ssi263_blazie")
DLL = os.path.join(REPO, "nvda", "dist", "blazie-lib", "x64" if sys.maxsize > 2 ** 32 else "x86", "bl.dll")
SPANISH = "--es" in sys.argv
BREAK = os.environ.get("RUN_AHEAD_STATE_BREAK", "")   # "1": one RAM byte changed; "settle": run_ahead.h's control
RA_BRK_SETTLE = 6                                      # run_ahead.h's RA_BRK_* order (run_ahead_equiv.py likewise)
RA_BRK_HELD = 10
RATE = 22050
BLOCK = 0.03
RAM_LO = 0x40000

EN = [("say", ["Hello, this is the first line.", "And a second line follows it."], "said"),
      ("send", b"\x0514E", "setting"),
      ("say", ["The rate is faster now."], "faster"),
      ("cancel", ["One line to be cut short, early on.", "Another line it never reaches.", "And a third one."],
       "cancelled"),
      ("say", ["Respoken at once."], "respoken"),
      ("idle", 12.0, "clicked off"),
      ("say", ["After the click off."], "after")]
ES = [("say", ["Hola, esta es la primera línea.", "Y la segunda la sigue."], "said"),
      ("send", b"\x0514E", "setting"),
      ("say", ["Ahora la velocidad es mayor."], "faster"),
      ("cancel", ["Una línea que se corta pronto.", "Otra que nunca llega.", "Y una tercera."], "cancelled"),
      ("say", ["Dicho otra vez."], "respoken"),
      ("idle", 12.0, "clicked off"),
      ("say", ["Después del clic."], "after")]
STEPS = ES if SPANISH else EN
if "--no-cancel" in sys.argv:                  # the continuation without the cancel (its open finding: see above)
    STEPS = [st for st in STEPS if st[2] not in ("cancelled", "respoken")]
# the lockstep variants that differ from the reference only in timing: its step, and more idle before a checkpoint
TIMING = [(0.00025, 0.0), (0.000125, 0.0), (0.0005, 0.137), (0.0005, 0.061), (0.00025, 0.2)]
# the stack's page (SP = D3FE at every checkpoint; its physical page inferred from where timing alone differs, and
# checked): the dead cells below the pointer hold whatever the last interrupts left there
STACK = (0x41300, 0x413FE)
FLOW = (0x11, 0x13)                         # XON, XOFF
HOST_INTS = ("sent_f", "echo_f", "stale_f", "held")
# After a cancel the run-ahead unit has read further than the lockstep's (it was cut at the capture's frontier), and
# ^X met its firmware at another point.  Two RAM areas keep that, and only these are named here -- each from the
# retained cancel trace (a trace build's watchpoints: which routine writes them, and when):
RESIDUE = [
    # written only by the firmware's staging routine, one character at a time from the start of each line: the
    # run-ahead unit had staged part of the cancelled utterance's next line before ^X; the next line is staged from
    # 46000 again, over it, and the rest is never read
    ("the line-staging buffer", 0x46000, 0x460FF),
    # written only by its banked-call routine, three bytes per nesting level (... 421A5, 421A8, 421AB, ...): like the
    # stack below SP, the level ^X's interrupt used depends on what it interrupted.  421A5-421A7 since 0.7 (the MAME
    # core, the lockstep settling before ^X as run ahead does): the English trace build's watchpoints show them
    # written by the same routine (5566/5568/556A) as 421A8-421AA, one level lower -- the lockstep's ^X handling
    # saved 1BCE there, the run-ahead unit's last call before ^X left BC38
    ("the banked-call save slots", 0x421A5, 0x421BF),
]
# ... and at the cancelled checkpoint only (the next say resets it; 'respoken' agrees): the firmware's text index at
# 41617 (Spanish: advanced at 0C6C while a line is processed, reset at B1D0 as the next begins; English, the same
# pair at 0C72 and BC70) stood elsewhere.  English too since 0.7: both units now settle before ^X, and the index
# then counts the frames each loaded after it -- the lockstep's cancel cut a second time (its unit loaded a pause
# after the first ^X) and advanced it 70 -> 73 (the trace build's watchpoints), run ahead's left it at 70
CUT_ONLY = [("the text index where the cut line stood", 0x41617, 0x41617)]
# At every checkpoint: the firmware's seconds counters, each incremented by its timer interrupt (0EB1) once per
# 6.18 M CPU cycles (1.006 s): they count CPU time, which run ahead does not keep (its unit ran ahead, then waited)
CLOCK = [("the firmware's seconds counters", 0x41436, 0x41436), ("the firmware's seconds counters", 0x41637, 0x41637)]
PROBE_SKIP = ("cycles", "pc", "sp")        # reported, not compared (the clock; where its idle loop happens to be)


def session(ahead, step=0.0005, extra_idle=0.0):
    if SPANISH:
        fw, st, enc = os.path.join(ENG, "BL2SPA.BNS"), os.path.join(ENG, "bl2spa_fresh.state"), "cp850"
    else:
        fw, st, enc = os.path.join(ENG, "BL2ENG.BNS"), os.path.join(ENG, "bl2_2003_warm.state"), "latin-1"
    chip = SSI263C(params={"closure_noise_lead_ms": 10.0}, out_rate=RATE)
    log = []
    u = NativeBlazie(DLL, fw, st, chip=chip, out_rate=RATE, menu=("punct_none", "numbers_toggle"), key_start=3000000,
                     key_gap=1500000, board_lowpass_hz=5000.0, on_write=lambda t, r, v: log.append((t, r, v)))
    u.encoding = enc
    u.send(b"\x18")
    u.send(b"\r\x06")
    u.run(0.3, step)
    u.send(b"\x056V")
    u.run(0.05, step)
    u.turbo_between_lines = True
    u.run_ahead = 1 if ahead else 0
    if ahead and BREAK == "settle":            # the control: the cancel meets the unit where the capture stopped it
        u.run_ahead_break = RA_BRK_SETTLE
    elif ahead:
        # The cancel's CR (bl_host.c cut_kept_line, issue 30) is left out HERE (RA_BRK_HELD): it gives the unit one
        # byte the lockstep's never gets, and every checkpoint from 'cancelled' on then differs by that byte's
        # footprint -- English: the receive buffer's two indices one further (41E15, 41E17), the last byte taken a CR
        # for ^X (43479), the buffer's bytes one place on (41E19...); Spanish: its line buffers one place on
        # (41659..., 4194C...).  Seen, read cell by cell, not classified here.  So this tool still compares the two
        # units' cancels as it did; what the CR does is run_ahead_kept_line.py's, and that the next utterance is its
        # own text with the CR in place is run_ahead_cancel.py's (English and Spanish)
        u.run_ahead_break = RA_BRK_HELD
    else:
        # ... and out of the lockstep unit too, which has had the same CR since 0.9.5 (bl_host.c bh_cancel): the two
        # units' cancels compared as they were, the CR's own effect tested where it is
        u.run_ahead_break = RA_BRK_HELD
    tx0 = [0]

    def idle(seconds):
        t = 0.0
        while t < seconds - 1e-9:
            u.run(min(BLOCK, seconds - t), step)
            t += min(BLOCK, seconds - t)
        u._drain()

    def point(name, n0):
        # the checkpoint proper: the unit asleep in its idle loop (SLP, its stack back at D3FE), not inside an
        # interrupt; up to 50 ms more, 1 ms at a time -- and if it never gets there, the checkpoint says so (a FAIL)
        for _ in range(50):
            p = u.probe()
            if p["sleeping"] and p["sp"] == 0xD3FE:
                break
            u.run(0.001, step)
        u._drain()
        p = u.probe()
        tx = bytes(u.tx)
        step_tx, tx0[0] = tx[tx0[0]:], len(tx)
        return dict(name=name, probe=p, ram=u.memory(0)[RAM_LO:], flash=u.memory(1), tx=step_tx,
                    reached=bool(p["sleeping"] and p["sp"] == 0xD3FE),
                    host={k: u.geti(k) for k in HOST_INTS}, owed=u.owed(), chip=[chip.regs[i] for i in range(5)],
                    request=bool(chip.request), writes=[(r, v) for _, r, v in log[n0:]], state=u.run_ahead_state,
                    settled=u.geti("run_ahead_settled"))
    points = [point("boot", 0)]
    for kind, arg, name in STEPS:
        u._drain()
        n0 = len(log)
        if kind == "send":
            u.send(arg)
            idle(0.02 + 0.5 + extra_idle)
        elif kind == "idle":
            idle(arg + extra_idle)
        elif kind == "say":
            u.say(arg)
            for _ in range(int(30 / BLOCK)):
                u.run(BLOCK, step)
                u._drain()
                if not u.busy():
                    break
            else:
                raise RuntimeError("%s: never done" % name)
            idle(0.5 + extra_idle)
        else:
            u.say(arg)
            idle(0.5)
            u.cancel()
            u._drain()
            if extra_idle:
                idle(extra_idle)
        points.append(point(name, n0))
    u.close()
    return points


def diff_bytes(a, b, base):
    if len(a) != len(b):                   # never compare only the shorter length
        raise ValueError("memory lengths differ: %d/%d bytes from %05X" % (len(a), len(b), base))
    out, n = [], len(a)
    for k in range(0, n, 4096):
        if a[k:k + 4096] != b[k:k + 4096]:
            out.extend(base + k + i for i in range(min(4096, n - k)) if a[k + i] != b[k + i])
    return out


def named_cell(x, name, cut):
    """the named, justified difference a RAM cell belongs to at checkpoint `name` (cut: from the cancelled one on)"""
    lists = CLOCK + (RESIDUE if cut else []) + (CUT_ONLY if name == "cancelled" else [])
    return next((r for r in lists if r[1] <= x <= r[2]), None)


def collapse(flow):
    """XON/XOFF with repeats collapsed: an XON sent while the host already sends is redundant (the flow state holds)"""
    return bytes(x for i, x in enumerate(flow) if i == 0 or flow[i - 1] != x)


def main():
    ref = session(False)
    ahd = session(True)
    if BREAK == "1":                   # the control: one RAM byte, away from anything timing touches
        p = ahd[2]
        k = len(p["ram"]) - 0x100
        p["ram"] = p["ram"][:k] + bytes([p["ram"][k] ^ 0x5A]) + p["ram"][k + 1:]
    # what timing alone moves: lockstep runs that differ only in their timing -- per checkpoint, never a union over
    # the session (a difference timing makes at one checkpoint must not hide one at another)
    timing = [session(False, step=s, extra_idle=x) for s, x in TIMING]
    mask_ram, mask_flash, mask_probe = [set() for _ in ref], [set() for _ in ref], [set() for _ in ref]
    for var in timing:
        for i, (a, b) in enumerate(zip(ref, var)):
            mask_ram[i].update(diff_bytes(a["ram"], b["ram"], RAM_LO))
            mask_flash[i].update(diff_bytes(a["flash"], b["flash"], 0))
            mask_probe[i].update(k for k in a["probe"] if a["probe"][k] != b["probe"][k])
    all_ram = set().union(*mask_ram)
    print("emulated time (Z180 cycles, the chip model): not a measurement of a real unit")
    print("timing alone (%d lockstep variants: steps 0.125-0.5 ms, 0.06-0.2 s more idle), per checkpoint: %s RAM "
          "cells%s, %d flash bytes, probe %s"
          % (len(TIMING), "/".join(str(len(m)) for m in mask_ram),
             (" (%05X-%05X)" % (min(all_ram), max(all_ram))) if all_ram else "", len(set().union(*mask_flash)),
             ", ".join(sorted(set().union(*mask_probe))) or "none"))
    unreached = [(p["name"], label) for label, pts in [("lockstep", ref), ("run ahead", ahd)]
                 + [("timing variant %d" % j, v) for j, v in enumerate(timing)] for p in pts if not p["reached"]]
    if unreached:
        print("FAIL checkpoints never reached the idle loop (asleep, SP D3FE) within 50 ms: %s" % unreached)
        return 1
    under = [x for x in all_ram if 0x41300 <= x < 0x41400]
    if not under or any(x >= STACK[1] for x in under) or any(p["probe"]["sp"] != 0xD3FE for p in ref + ahd):
        print("FAIL the stack inference: SP %s, timing-alone cells under 41400 %s" % (
            sorted(set("%04X" % p["probe"]["sp"] for p in ref + ahd)), ["%05X" % x for x in under]))
        return 1
    print("stack: SP D3FE at every checkpoint; the timing-alone cells under 41400 all lie in %05X-%05X, below it: its "
          "page taken as %05X-%05X (the common area at +34000h: inferred, not read from the MMU)"
          % (min(under), max(under), STACK[0], STACK[1] - 1))
    failures = 0
    cut = False                        # from the cancelled checkpoint on
    for i, (a, b) in enumerate(zip(ref, ahd)):
        name = a["name"]
        cut = cut or name == "cancelled"
        found, classified = [], []
        try:
            ram = diff_bytes(a["ram"], b["ram"], RAM_LO)
            fl = [x for x in diff_bytes(a["flash"], b["flash"], 0) if x not in mask_flash[i]]
        except ValueError as e:
            found.append(str(e))
            ram, fl = [], []
        stack = [x for x in ram if x not in mask_ram[i] and STACK[0] <= x < STACK[1]]
        named, extra = {}, []
        for x in ram:
            if x in mask_ram[i] or x in stack:
                continue
            r = named_cell(x, name, cut)
            if r:
                named.setdefault(r[0], []).append(x)
            else:
                extra.append(x)
        if stack:
            classified.append("%d stack cells below SP" % len(stack))
        for what, cells in named.items():
            classified.append("%s: %s" % (what, " ".join("%05X %02X/%02X" % (x, a["ram"][x - RAM_LO],
                                                                                b["ram"][x - RAM_LO]) for x in cells[:3])
                                           + (" ... (%d cells)" % len(cells) if len(cells) > 3 else "")))
        if extra:
            found.append("RAM %d cells beyond timing's: %s" % (len(extra), " ".join(
                "%05X %02X/%02X" % (x, a["ram"][x - RAM_LO], b["ram"][x - RAM_LO]) for x in extra[:8])))
        if fl:
            found.append("file flash %d bytes" % len(fl))
        for k in a["probe"]:
            if k in PROBE_SKIP or a["probe"][k] == b["probe"][k]:
                continue
            (classified if k in mask_probe[i] else found).append("%s %s/%s" % (k, a["probe"][k], b["probe"][k]))
        side = []
        # the serial bytes the unit sent during THIS step (a difference in one step is not carried into the next)
        data_a, data_b = bytes(x for x in a["tx"] if x not in FLOW), bytes(x for x in b["tx"] if x not in FLOW)
        flow_a, flow_b = bytes(x for x in a["tx"] if x in FLOW), bytes(x for x in b["tx"] if x in FLOW)
        if data_a != data_b:
            side.append("serial DATA bytes %s/%s" % (data_a.hex(), data_b.hex()))
        if flow_a != flow_b and collapse(flow_a) == collapse(flow_b):
            classified.append("a redundant XON (%s/%s; the flow state the same)" % (flow_a.hex(), flow_b.hex()))
        elif flow_a != flow_b:
            side.append("XON/XOFF sequence %s/%s" % (flow_a.hex(), flow_b.hex()))
        elif a["tx"] != b["tx"]:
            classified.append("XON/XOFF interleaved with ^F otherwise (%d bytes)" % len(a["tx"]))
        for k in HOST_INTS:
            if a["host"][k] != b["host"][k]:
                side.append("%s %d/%d" % (k, a["host"][k], b["host"][k]))
        if a["owed"] != b["owed"]:
            side.append("owed %d/%d" % (a["owed"], b["owed"]))
        (classified if name == "cancelled" else found).extend(side)
        if a["chip"] == b["chip"] and a["request"] != b["request"] and name == "cancelled":
            # the same registers, the unit's own pause after its ^X: only where that pause's timer stands differs
            classified.append("chip request %d/%d (registers identical: the pause after ^X, its timer)" % (
                a["request"], b["request"]))
        elif a["chip"] != b["chip"] or a["request"] != b["request"]:
            found.append("chip registers %s/%s, request %d/%d" % (a["chip"], b["chip"], a["request"], b["request"]))
        if name == "cancelled":
            classified.append("the unit settled before ^X: %s" % {1: "waited for an interrupt", 0: "at the cap",
                                                                  -1: "no capture running"}.get(b["settled"], "?"))
        wa, wb = a["writes"], b["writes"]
        if name == "cancelled":
            n = min(len(wa), len(wb))
            k = next((i for i in range(n) if wa[i] != wb[i]), None)
            if k is not None:
                classified.append("writes: a CANCEL-PREFIX match over %d of %d/%d" % (k, len(wa), len(wb)))
            else:
                classified.append("writes: a CANCEL-PREFIX match over all %d of %d/%d" % (n, len(wa), len(wb)))
        elif wa != wb:
            k = next((i for i in range(min(len(wa), len(wb))) if wa[i] != wb[i]), min(len(wa), len(wb)))
            found.append("writes DIFFER at %d of %d/%d" % (k, len(wa), len(wb)))
        failures += bool(found)
        cyc = "CPU %.3f/%.3f s" % (a["probe"]["cycles"] / 6144000.0, b["probe"]["cycles"] / 6144000.0)
        what = ("%d RAM cells within timing's" % len(ram)) if ram else "RAM identical"
        print("%-4s %-11s %s; %s; A0 %02X/%02X; run ahead %s%s%s" % (
            "FAIL" if found else "ok", name, cyc, what, a["probe"]["port_a0"], b["probe"]["port_a0"], b["state"],
            (" | CLASSIFIED: " + "; ".join(classified)) if classified else "",
            (" | FINDING: " + "; ".join(found)) if found else ""))
    lang = "Spanish" if SPANISH else "English"
    print("run ahead state (%s): %s" % (lang, "all checkpoints ok" if not failures else "%d FAILED" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
