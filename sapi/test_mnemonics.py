"""The SAPI settings dialog's access keys are each its own (issue #40).

'&Unregister' and 'Check for &updates...' both had U.  WinForms gives an access key to the first control after the
focused one, in tab order, that has it, so from the voice list (where the dialog opens), from Register, Close and
every setting, Alt+U -- which a screen reader announces for Check for updates -- clicked Unregister: a UAC prompt,
and every SSI-263 voice gone from SAPI once it was accepted.

This reads settings.ps1: every control text it sets (".Text = '...'") with an access key (an "&" before a letter, not
"&&"), and no two with the same letter.

    python sapi/test_mnemonics.py            exit 1 on any failure
    SSI263_MNEMONIC_BREAK=1   Unregister's key read as it was, U    -- must fail: U twice
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    text = open(os.path.join(HERE, "settings.ps1"), encoding="utf-8").read()
    if os.environ.get("SSI263_MNEMONIC_BREAK"):
        text = text.replace("'Unre&gister'", "'&Unregister'")
    keys = {}
    for m in re.finditer(r"\.Text\s*=\s*'([^']*)'", text):
        label = m.group(1).replace("&&", "")
        k = re.search(r"&([A-Za-z0-9])", label)
        if k:
            keys.setdefault(k.group(1).lower(), []).append(m.group(1))
    twice = {k: v for k, v in keys.items() if len(v) > 1}
    for k, v in sorted(keys.items()):
        print("%-4s Alt+%s  %s" % ("FAIL" if len(v) > 1 else "ok", k.upper(), " / ".join(v)))
    ok = len(keys) >= 10 and not twice
    print("settings access keys: %s" % ("%d controls, each key its own" % sum(len(v) for v in keys.values()) if ok
                                        else "%s twice" % ", ".join(k.upper() for k in sorted(twice)) if twice
                                        else "too few found (%d): the dialog's layout changed?" % len(keys)))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
