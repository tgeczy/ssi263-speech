"""run_tests.py's must-fail judgement, tested with fake children through its real run() (after Astra, Reply 97,
whose fixtures made the generic path accept a silent exit 42 and a native crash).  A control passes only when it
fails the way it names: its exit code AND every mark of its intended failure.

    python run_tests_guard.py
"""
import os
import sys

sys.argv = [sys.argv[0]]
import run_tests as R  # noqa: E402   (importable: the suite runs only as __main__)

MARKS = [r"^FAIL +the thing:", r"^thing: 1 FAILED$"]
INTENDED = 'print("ok   first case"); print("FAIL the thing: differs"); print("thing: 1 FAILED")'
# a native crash as the parent sees one: Windows' access-violation status (0xC0000005) with no output (ctypes would
# turn a real one into a Python OSError, which is the traceback case); SIGSEGV elsewhere
CRASH = ("import os; os._exit(-1073741819)" if os.name == "nt"
         else "import os, signal; os.kill(os.getpid(), signal.SIGSEGV)")

CASES = [   # (name, the child's code, the control must be accepted?)
    ("the intended failure, exit 1", INTENDED + "; raise SystemExit(1)", True),
    ("exit 42, no output", "raise SystemExit(42)", False),
    ("a native crash, no output", CRASH, False),
    ("exit 1 with a traceback", INTENDED + "; raise RuntimeError('broken build')", False),
    ("the intended text but exit 0", INTENDED, False),
    ("exit 1, a different failure", 'print("FAIL something else"); raise SystemExit(1)', False),
    ("exit 1, inventory not completed", 'print("FAIL the thing: differs"); raise SystemExit(1)', False),
    ("exit 1, empty output", "raise SystemExit(1)", False),
]

bad = 0
for name, code, want in CASES:
    c = R.check("fixture: " + name, [sys.executable, "-c", code], cwd=R.HERE, expect_fail=True, fail_marks=MARKS)
    _n, passed, _s, why, _last = R.run(c)
    ok = passed == want
    bad += not ok
    print("%-4s %-34s -> %s" % ("ok" if ok else "FAIL", name, "accepted" if passed else "rejected: " + why[:70]))
passed, why = R.judge_control(R.check("t", ["x"], expect_fail=True, fail_marks=MARKS), None, "", True)
ok = not passed
bad += not ok
print("%-4s %-34s -> %s" % ("ok" if ok else "FAIL", "a timeout", "accepted" if passed else "rejected: " + why))
# A positive output predicate supplements successful exit; it must never replace it.
# Previously matching text followed by exit 42 was accepted (Astra, release review).
for name, code, want in (
        ("positive text and successful exit", 'print("EXPECTED")', True),
        ("positive text then exit 42", 'print("EXPECTED"); raise SystemExit(42)', False),
        ("positive text then traceback", 'print("EXPECTED"); raise RuntimeError("broken")', False),
        ("successful exit, missing text", 'print("WRONG")', False)):
    c = R.check("fixture: " + name, [sys.executable, "-c", code], cwd=R.HERE,
                ok=lambda out: "EXPECTED" in out)
    _n, passed, _s, why, _last = R.run(c)
    ok = passed == want
    bad += not ok
    print("%-4s %-34s -> %s" % ("ok" if ok else "FAIL", name, "accepted" if passed else "rejected: " + why))
# --quick runs every check and no control: a quick run that dropped a check would pass on less than it says; one that
# kept the controls would not be quick.  Held against the suite's own registered checks.
quick, full = R.selected(R.CHECKS, True), R.selected(R.CHECKS, False)
lost = [c["name"] for c in R.CHECKS if not R.is_control(c) and c not in quick]
kept = [c["name"] for c in quick if R.is_control(c)]
for what, names in (("quick mode runs every check", lost), ("quick mode runs no control", kept),
                    ("the full run runs everything", [c["name"] for c in R.CHECKS if c not in full])):
    bad += bool(names)
    print("%-4s %s%s" % ("FAIL" if names else "ok", what, ": not so for %d, e.g. %s" % (len(names), names[0])
                         if names else " (%d of %d run)" % (len(quick if "quick" in what else full), len(R.CHECKS))))
print("control judgement: %s" % ("PASS" if not bad else "%d FAILED" % bad))
sys.exit(1 if bad else 0)
