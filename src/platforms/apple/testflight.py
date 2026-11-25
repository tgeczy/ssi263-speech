#!/usr/bin/env python3
"""TestFlight for SSI-263 Speech, macOS and iOS: TGSpeechBox's testflight.py, its lessons kept.

    python src/platforms/apple/testflight.py build [--bump] [--ios-only | --macos-only]
        SSI263Core and the Xcode project made fresh, every archive and export folder emptied first, the Release archives
        made and exported (app-store-connect), then verified before anything leaves the Mac: the build number the same
        in each app and its speech extension (and the Audio Unit's version), the icon and the privacy manifests inside,
        and no firmware at all (test/check_archive_no_firmware.py).  --bump raises CURRENT_PROJECT_VERSION (and the
        Audio Unit's version) by one first.
    python src/platforms/apple/testflight.py upload
        Uploads exactly what `build` made and verified -- never a fresh archive -- with altool, then waits for App Store
        Connect to process it and makes sure the internal group (every build, no review) is there.
    python src/platforms/apple/testflight.py status        the builds App Store Connect has, newest first
    python src/platforms/apple/testflight.py crashes       their crash signatures

TGSpeechBox's rules (its memory, testflight-ops):
- the same build number on both platforms, one higher than the last uploaded (macOS needs it to rise across versions);
- never retry an upload that seems to have failed: altool exits 0 on failure and a duplicate build number is burned
  for ever -- look (`status`) a few minutes later first;
- an upload is the archive that was checked: `build` then `upload`, never one step that re-archives.

Needs the team's App Store Connect key (~/.appstoreconnect/private_keys/AuthKey_<KEY_ID>.p8) and PyJWT + cryptography
(the repository's .venv).  MIT.
"""
import argparse
import json
import os
import plistlib
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request

KEY_ID = "BVT5H3NP87"                       # the team's key, TGSpeechBox's
ISSUER_ID = "69a6de72-61a0-47e3-e053-5b8c7c11a4d1"
KEY_FILE = os.path.expanduser("~/.appstoreconnect/private_keys/AuthKey_%s.p8" % KEY_ID)
APP_ID = "6821504895"                       # "SSI-263 Speech", com.ssi263speech.app (Tomi, 2026-10-10)
TEAM_ID = "P84QQE42U9"
INTERNAL_GROUP = "Team"                     # every build, no review

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
PROJECT_YML = os.path.join(HERE, "project.yml")
EXT_PLIST = os.path.join(HERE, "extension", "Info.plist")
XCODEPROJ = os.path.join(HERE, "SSI263Speech.xcodeproj")
OUT = os.path.join(REPO, "build", "apple", "testflight")
DERIVED = os.path.join(OUT, "derived")
PLATFORMS = {
    "macOS": ("SsiSpeech-macOS", "generic/platform=macOS", "macos", ".pkg"),
    "iOS": ("SsiSpeech-iOS", "generic/platform=iOS", "ios", ".ipa"),
}


# ---- App Store Connect -------------------------------------------------------------------------------------------

def token():
    import jwt
    now = int(time.time())
    with open(KEY_FILE) as f:
        key = f.read()
    return jwt.encode({"iss": ISSUER_ID, "iat": now, "exp": now + 1200, "aud": "appstoreconnect-v1"}, key,
                      algorithm="ES256", headers={"kid": KEY_ID, "typ": "JWT"})


def api(method, path, body=None):
    req = urllib.request.Request("https://api.appstoreconnect.apple.com" + path, method=method,
                                 data=json.dumps(body).encode() if body is not None else None,
                                 headers={"Authorization": "Bearer " + token(), "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            data = r.read()
            return json.loads(data) if data else {}
    except urllib.error.HTTPError as e:
        raise SystemExit("App Store Connect %s %s: HTTP %d %s" % (method, path, e.code, e.read().decode()[:400]))


def builds(version=None):
    q = "/v1/builds?filter%%5Bapp%%5D=%s&sort=-uploadedDate&limit=20&include=preReleaseVersion" \
        "&fields%%5Bbuilds%%5D=version,processingState,uploadedDate,preReleaseVersion" \
        "&fields%%5BpreReleaseVersions%%5D=platform,version" % APP_ID
    if version:
        q += "&filter%%5Bversion%%5D=%s" % version
    data = api("GET", q)
    plat = {p["id"]: p["attributes"] for p in data.get("included", []) if p["type"] == "preReleaseVersions"}
    out = []
    for b in data.get("data", []):
        pre = plat.get((b["relationships"]["preReleaseVersion"].get("data") or {}).get("id"), {})
        out.append(dict(b["attributes"], id=b["id"], platform=pre.get("platform", "?"), train=pre.get("version", "?")))
    return out


def internal_group():
    """The internal group with every build (made when it is not there); its id."""
    groups = api("GET", "/v1/apps/%s/betaGroups?fields%%5BbetaGroups%%5D=name,isInternalGroup" % APP_ID)["data"]
    for g in groups:
        if g["attributes"]["isInternalGroup"]:
            return g["id"], g["attributes"]["name"]
    made = api("POST", "/v1/betaGroups", {"data": {
        "type": "betaGroups", "attributes": {"name": INTERNAL_GROUP, "isInternalGroup": True, "hasAccessToAllBuilds": True},
        "relationships": {"app": {"data": {"type": "apps", "id": APP_ID}}}}})["data"]
    print("  made the internal group %r: add yourself in App Store Connect, TestFlight, %s" % (INTERNAL_GROUP,
                                                                                             INTERNAL_GROUP))
    return made["id"], INTERNAL_GROUP


# ---- the build number ---------------------------------------------------------------------------------------------

def build_number():
    m = re.search(r'CURRENT_PROJECT_VERSION:\s*"(\d+)"', open(PROJECT_YML).read())
    return int(m.group(1))


def marketing_version():
    return re.search(r'MARKETING_VERSION:\s*"([^"]+)"', open(PROJECT_YML).read()).group(1)


def bump():
    old = build_number()
    new = old + 1
    text = open(PROJECT_YML).read().replace('CURRENT_PROJECT_VERSION: "%d"' % old, 'CURRENT_PROJECT_VERSION: "%d"' % new)
    open(PROJECT_YML, "w").write(text)
    sync_component_version(new)
    print("  build number %d -> %d (project.yml, the Audio Unit's version)" % (old, new))
    return new


def sync_component_version(n):
    """The Audio Unit's version in the extension's Info.plist (an <integer>: no build setting reaches it)."""
    text = open(EXT_PLIST).read()
    new = re.sub(r"(<key>version</key>\s*<integer>)\d+(</integer>)", r"\g<1>%d\g<2>" % n, text)
    if new != text:
        open(EXT_PLIST, "w").write(new)


# ---- build --------------------------------------------------------------------------------------------------------

def run(cmd, what, cwd=HERE):
    print("  %s..." % what)
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode:
        print("  FAILED: %s" % " ".join(cmd))
        for line in (r.stdout + "\n" + r.stderr).strip().splitlines()[-25:]:
            print("    " + line)
        sys.exit(1)
    return r.stdout


# Signing (the archive's cloud-managed distribution certificates) through the team's App Store Connect key, as the
# upload does, never through an Apple ID signed in to Xcode: Xcode 27's update dropped that sign-in, and every export
# then failed ("Unable to log in with account", no "Mac App Distribution" certificate).
def key_flags():
    return ["-authenticationKeyPath", KEY_FILE, "-authenticationKeyID", KEY_ID, "-authenticationKeyIssuerID", ISSUER_ID]


def archive(name):
    scheme, dest, _, ext = PLATFORMS[name]
    path = os.path.join(OUT, name + ".xcarchive")
    run(["xcodebuild", "archive", "-project", XCODEPROJ, "-scheme", scheme, "-configuration", "Release",
         "-destination", dest, "-archivePath", path, "-derivedDataPath", DERIVED, "-allowProvisioningUpdates"]
        + key_flags(),
        "archiving %s" % name)
    opts = os.path.join(OUT, "ExportOptions.plist")
    with open(opts, "wb") as f:
        plistlib.dump({"method": "app-store-connect", "teamID": TEAM_ID, "signingStyle": "automatic",
                       "uploadSymbols": True, "destination": "export"}, f)
    export = os.path.join(OUT, name + "-export")
    run(["xcodebuild", "-exportArchive", "-archivePath", path, "-exportPath", export, "-exportOptionsPlist", opts,
         "-allowProvisioningUpdates"] + key_flags(), "exporting %s" % name)
    artifacts = [f for f in os.listdir(export) if f.endswith(ext)]
    if len(artifacts) != 1:
        sys.exit("  %s: expected one %s in %s, found %s" % (name, ext, export, artifacts))
    return path, os.path.join(export, artifacts[0])


# The privacy manifest's keys, and the required-reason codes this app may declare (Apple's "Describing use of
# required reason API"): build 1's manifests, valid plists with an XML comment, were refused as invalid (ITMS-91056).
MANIFEST_KEYS = {"NSPrivacyTracking", "NSPrivacyTrackingDomains", "NSPrivacyCollectedDataTypes",
                 "NSPrivacyAccessedAPITypes"}
REASONS = {"NSPrivacyAccessedAPICategoryUserDefaults": {"CA92.1", "1C8F.1", "C56D.1", "AC6B.1"},
           "NSPrivacyAccessedAPICategoryFileTimestamp": {"DDA9.1", "C617.1", "3B52.1", "0A2A.1"}}


def manifest_problems(path):
    """What App Store Connect could refuse in a privacy manifest: comments, other keys, unknown reasons."""
    raw = open(path, "rb").read()
    if b"<!--" in raw:
        return ["an XML comment"]
    try:
        m = plistlib.loads(raw)
    except Exception as e:
        return ["not a plist (%s)" % e]
    bad = ["the key %s" % k for k in m if k not in MANIFEST_KEYS]
    if m.get("NSPrivacyTracking") is not False:
        bad.append("NSPrivacyTracking is not false")
    for t in m.get("NSPrivacyAccessedAPITypes", []):
        cat, reasons = t.get("NSPrivacyAccessedAPIType"), t.get("NSPrivacyAccessedAPITypeReasons", [])
        if cat not in REASONS:
            bad.append("the category %s" % cat)
        elif not reasons or set(reasons) - REASONS[cat]:
            bad.append("%s's reasons %s" % (cat, reasons))
    return bad


def verify(name, archive_path, n):
    """What goes up is what it should be: the numbers, the icon, the manifests, no firmware."""
    apps = os.path.join(archive_path, "Products", "Applications")
    app = os.path.join(apps, os.listdir(apps)[0])
    mac = name == "macOS"
    contents = os.path.join(app, "Contents") if mac else app
    appex = os.path.join(contents, "PlugIns", "SsiSynthExtension.appex")
    appex_contents = os.path.join(appex, "Contents") if mac else appex
    bad = []

    def plist(p):
        with open(p, "rb") as f:
            return plistlib.load(f)
    a, x = plist(os.path.join(contents, "Info.plist")), plist(os.path.join(appex_contents, "Info.plist"))
    for who, p in (("the app", a), ("the extension", x)):
        if p.get("CFBundleVersion") != str(n) or p.get("CFBundleShortVersionString") != marketing_version():
            bad.append("%s is %s (%s), not %s (%d)" % (who, p.get("CFBundleShortVersionString"),
                                                       p.get("CFBundleVersion"), marketing_version(), n))
    comps = x.get("NSExtension", {}).get("NSExtensionAttributes", {}).get("AudioComponents", [{}])
    if comps[0].get("version") != n:
        bad.append("the Audio Unit's version is %s, not %d" % (comps[0].get("version"), n))
    res = os.path.join(contents, "Resources") if mac else contents
    if not os.path.isfile(os.path.join(res, "Assets.car")):
        bad.append("no icon (Assets.car)")
    for who, d in (("the app", res), ("the extension", os.path.join(appex_contents, "Resources") if mac else appex)):
        mf = os.path.join(d, "PrivacyInfo.xcprivacy")
        if not os.path.isfile(mf):
            bad.append("%s has no privacy manifest" % who)
        else:
            bad += ["%s's privacy manifest has %s" % (who, p) for p in manifest_problems(mf)]
    audit = subprocess.run([sys.executable, os.path.join(HERE, "test", "check_archive_no_firmware.py"), archive_path],
                           capture_output=True, text=True)
    if audit.returncode:
        bad.append("firmware inside:\n" + audit.stdout)
    for b in bad:
        print("  FAIL  %s: %s" % (name, b))
    if not bad:
        print("  ok    %s: %s (%d) in the app and its extension, the icon, the privacy manifests, no firmware" % (
            name, marketing_version(), n))
    return not bad


def cmd_build(a):
    if a.bump:
        bump()
    n = build_number()
    sync_component_version(n)
    names = [p for p in PLATFORMS if not (a.ios_only and p == "macOS") and not (a.macos_only and p == "iOS")]
    print("=== SSI-263 Speech %s (%d): %s ===" % (marketing_version(), n, ", ".join(names)))
    # nothing left from an earlier run may be uploaded or linked: archives, exports, derived data, libraries
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    run(["sh", os.path.join(HERE, "build_apple.sh")], "SSI263Core, every slice")
    run(["xcodegen", "generate"], "the Xcode project")
    made = {}
    for name in names:
        made[name] = archive(name)
    ok = all([verify(name, made[name][0], n) for name in names])
    with open(os.path.join(OUT, "made.json"), "w") as f:
        json.dump({"build": n, "version": marketing_version(), "ok": ok,
                   "artifacts": {k: v[1] for k, v in made.items()}}, f, indent=1)
    print("\n%s" % ("ready to upload: python src/platforms/apple/testflight.py upload" if ok
                    else "NOT ready: fix the above and build again"))
    return 0 if ok else 1


# ---- upload -------------------------------------------------------------------------------------------------------

ASC_PLATFORM = {"macOS": "MAC_OS", "iOS": "IOS"}     # App Store Connect's names for PLATFORMS' keys


def cmd_upload(a):
    try:
        made = json.load(open(os.path.join(OUT, "made.json")))
    except OSError:
        sys.exit("nothing built: python src/platforms/apple/testflight.py build first")
    if not made["ok"]:
        sys.exit("the last build failed its checks: build again")
    n = str(made["build"])
    # a platform App Store Connect already has this number for is never sent again (a number once used is gone); the
    # other is (build 14: the macOS upload arrived, but was read as failed, and the iOS one never started)
    there = {b["platform"] for b in builds(n)}
    todo = {k: v for k, v in made["artifacts"].items() if ASC_PLATFORM[k] not in there}
    if not todo:
        sys.exit("build %s is on App Store Connect already: never upload a number twice (build --bump)" % n)
    for name in made["artifacts"]:
        if name not in todo:
            print("=== %s %s (%s) is on App Store Connect already: not sent again ===" % (name, made["version"], n))
    for name, artifact in todo.items():
        print("=== uploading %s %s (%s) ===" % (name, made["version"], n))
        r = subprocess.run(["xcrun", "altool", "--upload-app", "-f", artifact, "-t", PLATFORMS[name][2],
                            "--apiKey", KEY_ID, "--apiIssuer", ISSUER_ID], capture_output=True, text=True)
        out = (r.stdout + "\n" + r.stderr).strip()
        # altool exits 0 on failure: its words decide.  "UPLOAD SUCCEEDED" wins: Xcode 27's altool logs an ERROR line
        # about its resume file (CDUploaderStateInfoResumeFilePath) on an upload that succeeded
        ok = "UPLOAD SUCCEEDED" in out and "UPLOAD FAILED" not in out
        if not ok and (r.returncode or "UPLOAD FAILED" in out or "Failed to upload" in out or "ERROR" in out):
            print("\n".join("    " + line for line in out.splitlines()[-15:]))
            sys.exit("%s: the upload says it failed. Do NOT retry: run `status` in a few minutes first -- a build "
                     "may have arrived anyway, and a number once used is gone." % name)
        print("  %s uploaded" % name)
    group, gname = internal_group()
    print("=== waiting for App Store Connect to process build %s ===" % n)
    want = len(made["artifacts"])
    for attempt in range(60):
        got = builds(n)
        states = ["%s %s" % (b["platform"], b["processingState"]) for b in got]
        print("  %s" % (", ".join(states) or "not visible yet"))
        if len(got) >= want and all(b["processingState"] == "VALID" for b in got):
            print("build %s is on TestFlight for the internal group %r" % (n, gname))
            return 0
        if any(b["processingState"] in ("FAILED", "INVALID") for b in got):
            sys.exit("processing failed: App Store Connect's email says why")
        time.sleep(30)
    print("still processing after 30 minutes: run `status` later")
    return 0


def cmd_status(a):
    for b in builds():
        print("%-6s %-6s %-8s %-11s %s" % (b["platform"], b["train"], b["version"], b["processingState"],
                                         b["uploadedDate"]))
    return 0


def cmd_crashes(a):
    for b in builds()[:10]:
        sigs = api("GET", "/v1/builds/%s/diagnosticSignatures?limit=50" % b["id"]).get("data", [])
        print("%s %s (%s): %d crash signature(s)" % (b["platform"], b["version"], b["uploadedDate"], len(sigs)))
        for s in sigs:
            print("   [%s] %s" % (s["attributes"].get("diagnosticType"), s["attributes"].get("signature", "")[:200]))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("command", choices=["build", "upload", "status", "crashes"])
    ap.add_argument("--bump", action="store_true")
    ap.add_argument("--ios-only", action="store_true")
    ap.add_argument("--macos-only", action="store_true")
    a = ap.parse_args()
    if not os.path.isfile(KEY_FILE):
        sys.exit("no App Store Connect key at %s" % KEY_FILE)
    return {"build": cmd_build, "upload": cmd_upload, "status": cmd_status, "crashes": cmd_crashes}[a.command](a)


if __name__ == "__main__":
    sys.exit(main())
