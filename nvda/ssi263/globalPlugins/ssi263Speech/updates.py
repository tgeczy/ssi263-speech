# -*- coding: utf-8 -*-
"""Check for updates, by hand only (Tomi: never at start-up or on a timer -- a dialog that pops up steals focus, maybe
mid-typing, and NVDA has no notification centre to hold it).  After outSPOKEN's updates.py and Panthera's: GitHub's
releases, drafts and prereleases skipped, the version read from the asset's own name (so a release whose tag says
otherwise is still read right), and -- new here -- the download checked against the release's SHA256SUMS.txt before
NVDA is handed it.  It fails closed: no checksum file, no entry, two entries or a wrong hash, and nothing is installed.
(SHA256SUMS proves the file is the one that release published, not who made it: it is not a signature.)

No wx and no NVDA here: __init__.py runs these on a worker thread and shows the results.  Python 3.7 to 3.13.
"""
import hashlib
import json
import re
import urllib.request

REPO = "tgeczy/ssi263-speech"
API = "https://api.github.com/repos/%s/releases?per_page=20" % REPO
ASSET = re.compile(r"^ssi263-speech-(\d+(?:\.\d+)*)\.nvda-addon$")
SUMS = "SHA256SUMS.txt"
TIMEOUT = 30
USER_AGENT = "ssi263-speech-nvda-addon"


class UpdateError(Exception):
    """Why an update was not offered or not installed, in words for the user."""


def version_tuple(v):
    return tuple(int(x) for x in re.findall(r"\d+", str(v)))


def newest(releases, current):
    """(version, asset name, asset url, checksum url) of the newest add-on newer than `current` among GitHub's
    releases (the API's list), or None.  The version is the asset name's."""
    best = None
    for rel in releases:
        if rel.get("draft") or rel.get("prerelease"):
            continue
        assets = {a.get("name"): a.get("browser_download_url") for a in rel.get("assets") or []}
        for name, url in assets.items():
            m = ASSET.match(name or "")
            if not m or not url:
                continue
            v = version_tuple(m.group(1))
            if best is None or v > best[0]:
                best = (v, m.group(1), name, url, assets.get(SUMS))
    if best is None or best[0] <= version_tuple(current):
        return None
    return best[1:]


def expected_hash(sums_text, name):
    """The one SHA-256 SHA256SUMS.txt gives for `name` (sha256sum's "<hash>  <name>" or "<hash> *<name>")."""
    found = []
    for line in sums_text.splitlines():
        m = re.match(r"^([0-9a-fA-F]{64}) [ *](.+?)\s*$", line)
        if m and m.group(2) == name:
            found.append(m.group(1).lower())
    if not found:
        raise UpdateError("The release's checksum file has no entry for %s." % name)
    if len(found) > 1:
        raise UpdateError("The release's checksum file lists %s more than once." % name)
    return found[0]


def verify(data, sha256):
    got = hashlib.sha256(data).hexdigest()
    if got != sha256:
        raise UpdateError("The download does not match the release's checksum (got %s, expected %s)."
                          % (got[:12], sha256[:12]))


def _get(url, opener=None):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": "application/octet-stream"
                                               if "api.github.com" not in url else "application/vnd.github+json"})
    with (opener or urllib.request.urlopen)(req, timeout=TIMEOUT) as r:
        return r.read()


def check(current, fetch=_get):
    """None when `current` is the newest; else (version, asset name, asset url, checksum url)."""
    try:
        releases = json.loads(fetch(API).decode("utf-8"))
    except UpdateError:
        raise
    except Exception as e:
        raise UpdateError("Could not reach GitHub: %s" % e)
    if not isinstance(releases, list):
        raise UpdateError("GitHub's answer was not a list of releases.")
    return newest(releases, current)


def download(found, fetch=_get):
    """The add-on's bytes, checked against the release's SHA256SUMS.txt; raises UpdateError otherwise."""
    _version, name, url, sums_url = found
    if not sums_url:
        raise UpdateError("The release has no %s, so the download cannot be checked." % SUMS)
    try:
        sums = fetch(sums_url).decode("utf-8", "replace")
        data = fetch(url)
    except Exception as e:
        raise UpdateError("The download failed: %s" % e)
    verify(data, expected_hash(sums, name))
    return data
