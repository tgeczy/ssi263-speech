"""No path from anyone's own machine may be committed.

Walks every tracked text file (git ls-files) and fails on an absolute drive path, a home
folder, or an MSYS-style /c/ path.  Tools and builds find what lives outside the repo
through tools/repo_paths.py (an environment variable, or the gitignored paths.local), so a
fresh clone never depends on where one person keeps things.

Allowed: URLs (the drive-letter rule needs no letter before it, so "https://" never
matches), and NVDA's standard install folder used as an example argument.
The vendored third_party/ tree is someone else's text and is skipped.

    python tools/check_no_machine_paths.py        exit status 1 on any hit
"""
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Built from pieces so this file never matches itself.
DRIVE = r"(?<![A-Za-z0-9%])[A-Za-z]" + r":[\\/]"
PATTERNS = [
    re.compile(DRIVE),                                   # a drive letter, a colon and a slash
    re.compile(r"(?i)\bUsers" + r"[\\/][A-Za-z]"),        # a Windows home folder
    re.compile(r"(?i)(?<![A-Za-z0-9])/[a-z]/" + r"git\b"),  # an MSYS-style drive folder
    re.compile(r"(?i)/home" + r"/[a-z]"),                 # a Linux home folder
]
ALLOWED = [re.compile(r"(?i)[A-Z]" + r":\\\\?Program Files\\\\?NVDA")]
SKIP_PREFIXES = ("third_party/",)
SKIP_SUFFIXES = (".tar.gz", ".zip", ".BIN", ".DVC", ".wav", ".dll", ".exe", ".png")


def tracked_files():
    out = subprocess.run(["git", "ls-files", "-z"], cwd=REPO, capture_output=True, check=True).stdout
    return [p for p in out.decode("utf-8").split("\0") if p]


def hits_in(text):
    for n, line in enumerate(text.splitlines(), 1):
        cleaned = line
        for a in ALLOWED:
            cleaned = a.sub("", cleaned)
        if any(p.search(cleaned) for p in PATTERNS):
            yield n, line.strip()


def main():
    bad = 0
    for rel in tracked_files():
        if rel.startswith(SKIP_PREFIXES) or rel.endswith(SKIP_SUFFIXES):
            continue
        path = os.path.join(REPO, rel)
        if not os.path.isfile(path):
            continue
        with open(path, "rb") as f:
            data = f.read()
        if b"\0" in data:
            continue                                    # binary
        for n, line in hits_in(data.decode("utf-8", "replace")):
            print("%s:%d: %s" % (rel, n, line[:160]))
            bad += 1
    if bad:
        print("%d machine path(s) in tracked files; use tools/repo_paths.py instead" % bad)
        return 1
    print("no machine paths in tracked files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
