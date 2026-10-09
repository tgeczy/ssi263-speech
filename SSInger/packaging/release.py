"""SSInger's release download: assemble it, pack it, check it.

One script for all four CI jobs (Windows x64, macOS universal, Linux x86_64
and arm64), so the licence list lives in one place.  The download is a
folder named SSInger-<version>-<platform>, packed as .zip (Windows, macOS)
or .tar.gz (Linux):

    VST3/ CLAP/ Standalone/   (+ AU/ on macOS, LV2/ on Linux)  the plug-ins
    LICENSE                   this repository's MIT licence
    README.md, BUILD.md       SSInger's own (install folders, Gatekeeper, OSARA)
    note_map.txt              which key sings which phoneme (for reading: the plug-in has it built in)
    SOURCE.txt                the AGPLv3 corresponding-source offer
    licenses/                 AGPLv3, Casso, and every bundled third-party
                              notice, copied from the trees CMake fetched

    python SSInger/packaging/release.py version
    python SSInger/packaging/release.py build --platform linux-x86_64 \
        --artefacts build/SSInger_artefacts/Release --deps build/_deps \
        --out dist --repo-url URL --commit SHA [--run-url URL]
    python SSInger/packaging/release.py check dist/SSInger-0.7.0-linux-x86_64.tar.gz \
        --platform linux-x86_64 --commit SHA

The version is the project's release version (python/ssi263speech/_version.py,
the same 0.7.0 as the add-on manifests).  SSInger/CMakeLists.txt's own
project VERSION is what hosts are shown and is left alone here.
"""
import argparse
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SSINGER = os.path.dirname(HERE)
REPO = os.path.dirname(SSINGER)

PLATFORMS = ("windows-x64", "macos-universal", "linux-x86_64", "linux-arm64")
ALL = PLATFORMS
MAC = ("macos-universal",)
LINUX = ("linux-x86_64", "linux-arm64")

# (format folder, bundle name, a file inside the bundle that must be there or None)
BUNDLES = {
    "windows-x64": [
        ("VST3", "SSInger.vst3", "Contents/x86_64-win/SSInger.vst3"),
        ("CLAP", "SSInger.clap", None),
        ("Standalone", "SSInger.exe", None),
    ],
    "macos-universal": [
        ("VST3", "SSInger.vst3", "Contents/MacOS/SSInger"),
        ("AU", "SSInger.component", "Contents/MacOS/SSInger"),
        ("CLAP", "SSInger.clap", "Contents/MacOS/SSInger"),
        ("Standalone", "SSInger.app", "Contents/MacOS/SSInger"),
    ],
    "linux-x86_64": [
        ("VST3", "SSInger.vst3", "Contents/x86_64-linux/SSInger.so"),
        ("CLAP", "SSInger.clap", None),
        ("LV2", "SSInger.lv2", "manifest.ttl"),
        ("Standalone", "SSInger", None),
    ],
    "linux-arm64": [
        ("VST3", "SSInger.vst3", "Contents/aarch64-linux/SSInger.so"),
        ("CLAP", "SSInger.clap", None),
        ("LV2", "SSInger.lv2", "manifest.ttl"),
        ("Standalone", "SSInger", None),
    ],
}

M = "modules/"
HL = M + "juce_audio_processors_headless/format_types/"

# Every third-party piece in the binaries (SSInger/third_party/README.md):
# (folder under licenses/, its SPDX id in JUCE.spdx.json or None, licence,
#  platforms, [(tree, path in that tree)]).  Tree "juce" is the fetched JUCE,
# "cje" the fetched clap-juce-extensions.  An empty file list means the piece
# ships no licence file: its notice is written from JUCE.spdx.json.
PIECES = [
    ("JUCE", "SPDXRef-JUCE", "AGPL-3.0-only (JUCE's AGPLv3 option)", ALL,
     [("juce", "LICENSE.md"), ("juce", "JUCE.spdx.json")]),
    ("VST3_SDK", "SPDXRef-juce-audio-processors-headless.vst3-sdk", "MIT", ALL,
     [("juce", HL + "VST3_SDK/LICENSE.txt")]),
    ("pslextensions", "SPDXRef-juce-audio-processors-headless.pslextensions", "Public domain", ALL, []),
    ("AudioUnitSDK", "SPDXRef-juce-audio-plugin-client.audiounitsdk", "Apache-2.0", MAC,
     [("juce", M + "juce_audio_plugin_client/AU/AudioUnitSDK/LICENSE.txt"),
      ("juce?", M + "juce_audio_plugin_client/AU/AudioUnitSDK/NOTICE"),
      ("juce?", M + "juce_audio_plugin_client/AU/AudioUnitSDK/NOTICE.txt")]),
    ("LV2", "SPDXRef-juce-audio-processors-headless.lv2", "ISC", LINUX,
     [("juce", HL + "LV2_SDK/lv2/COPYING")]),
    ("serd", "SPDXRef-juce-audio-processors-headless.serd", "ISC", LINUX,
     [("juce", HL + "LV2_SDK/serd/COPYING")]),
    ("sord", "SPDXRef-juce-audio-processors-headless.sord", "ISC", LINUX,
     [("juce", HL + "LV2_SDK/sord/COPYING")]),
    ("sratom", "SPDXRef-juce-audio-processors-headless.sratom", "ISC", LINUX,
     [("juce", HL + "LV2_SDK/sratom/COPYING")]),
    ("lilv", "SPDXRef-juce-audio-processors-headless.lilv", "ISC", LINUX,
     [("juce", HL + "LV2_SDK/lilv/COPYING")]),
    ("clap-juce-extensions", None, "MIT", ALL, [("cje", "LICENSE.md")]),
    ("CLAP", None, "MIT", ALL, [("cje", "clap-libs/clap/LICENSE")]),
    ("clap-helpers", None, "MIT", ALL, [("cje", "clap-libs/clap-helpers/LICENSE")]),
    ("zlib", "SPDXRef-juce-core.zlib", "Zlib", ALL,
     [("juce", M + "juce_core/zip/zlib/LICENSE")]),
    ("libpng", "SPDXRef-juce-graphics.libpng", "libpng-2.0", ALL,
     [("juce", M + "juce_graphics/image_formats/pnglib/LICENSE")]),
    # IJG: the README carries the copyright and terms ("LEGAL ISSUES").
    ("jpeglib", "SPDXRef-juce-graphics.jpeglib", "IJG", ALL,
     [("juce", M + "juce_graphics/image_formats/jpglib/README")]),
    ("libwebp", "SPDXRef-juce-graphics.libwebp", "BSD-3-Clause", ALL,
     [("juce", M + "juce_graphics/image_formats/libwebp/COPYING"),
      ("juce", M + "juce_graphics/image_formats/libwebp/PATENTS")]),
    ("HarfBuzz", "SPDXRef-juce-graphics.harfbuzz", "MIT-Modern-Variant (Old MIT)", ALL,
     [("juce", M + "juce_graphics/fonts/harfbuzz/COPYING")]),
    ("SheenBidi", "SPDXRef-juce-graphics.sheenbidi", "Apache-2.0", ALL,
     [("juce", M + "juce_graphics/unicode/sheenbidi/LICENSE"),
      ("juce?", M + "juce_graphics/unicode/sheenbidi/NOTICE")]),
    ("LunaSVG", "SPDXRef-juce-graphics.lunasvg", "MIT", ALL,
     [("juce", M + "juce_graphics/drawables/lunasvg/LICENSE")]),
    ("PlutoVG", "SPDXRef-juce-graphics.plutovg", "MIT", ALL,
     [("juce", M + "juce_graphics/drawables/lunasvg/plutovg/LICENSE")]),
    ("FLAC", "SPDXRef-juce-audio-formats.flac", "BSD-3-Clause", ALL,
     [("juce", M + "juce_audio_formats/codecs/flac/Flac Licence.txt")]),
    ("libogg", "SPDXRef-juce-audio-formats.libogg", "BSD-3-Clause", ALL,
     [("juce", M + "juce_audio_formats/codecs/ogg/COPYING")]),
    ("libvorbis", "SPDXRef-juce-audio-formats.libvorbis", "BSD-3-Clause", ALL,
     [("juce", M + "juce_audio_formats/codecs/vorbis/COPYING")]),
    ("Opus", "SPDXRef-juce-audio-formats.opus", "BSD-3-Clause", ALL,
     [("juce", M + "juce_audio_formats/codecs/opus/opus/COPYING"),
      ("juce", M + "juce_audio_formats/codecs/opus/opus/LICENSE_PLEASE_READ.txt")]),
    ("opusfile", "SPDXRef-juce-audio-formats.opusfile", "BSD-3-Clause", ALL,
     [("juce", M + "juce_audio_formats/codecs/opus/opusfile/COPYING")]),
    ("libopusenc", "SPDXRef-juce-audio-formats.libopusenc", "BSD-3-Clause", ALL,
     [("juce", M + "juce_audio_formats/codecs/opus/libopusenc/COPYING")]),
]

# Files every download carries, in-archive path: source in this repo.
OWN_FILES = {
    "LICENSE": os.path.join(REPO, "LICENSE"),
    "README.md": os.path.join(SSINGER, "README.md"),
    "BUILD.md": os.path.join(SSINGER, "BUILD.md"),
    "note_map.txt": os.path.join(SSINGER, "note_map.txt"),   # which key sings what, for reading (Spacedog: so
                                                              # nobody gets confused); the plug-in has it built in
    "licenses/AGPL-3.0.txt": os.path.join(HERE, "AGPL-3.0.txt"),
    "licenses/Casso-MIT.txt": os.path.join(REPO, "third_party", "casso", "LICENSE"),
    "licenses/README.md": os.path.join(SSINGER, "third_party", "README.md"),
}
NOTICES_INDEX = "licenses/THIRD-PARTY-NOTICES.txt"


def release_version():
    text = open(os.path.join(REPO, "python", "ssi263speech", "_version.py"), encoding="utf-8").read()
    return re.search(r'__version__\s*=\s*"([^"]+)"', text).group(1)


def pins():
    """The JUCE tag and clap-juce-extensions commit, as SSInger/CMakeLists.txt pins them."""
    text = open(os.path.join(SSINGER, "CMakeLists.txt"), encoding="utf-8").read()
    juce = re.search(r"FetchContent_Declare\(JUCE\b.*?GIT_TAG\s+(\S+)", text, re.S).group(1)
    cje = re.search(r"FetchContent_Declare\(clap_juce_extensions\b.*?GIT_TAG\s+(\S+)", text, re.S).group(1)
    return juce, cje


def archive_name(version, platform):
    stem = "SSInger-%s-%s" % (version, platform)
    return stem, stem + (".tar.gz" if platform.startswith("linux") else ".zip")


def pieces_for(platform):
    return [p for p in PIECES if platform in p[3]]


def piece_dest(folder, path):
    return "licenses/%s/%s" % (folder, path.rsplit("/", 1)[-1])


def git_out(tree, *args):
    try:
        return subprocess.run(["git", "-C", tree] + list(args), capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def copy_bundle(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if sys.platform == "darwin":
        # ditto keeps the ad-hoc signatures, symlinks and extended attributes.
        subprocess.run(["ditto", src, dst], check=True)
    elif os.path.isdir(src):
        shutil.copytree(src, dst, symlinks=True)
    else:
        shutil.copy2(src, dst)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def source_txt(version, platform, repo_url, commit, run_url, trees):
    juce_tag, cje_commit = pins()
    juce_head = git_out(trees["juce"], "rev-parse", "HEAD")
    cje_head = git_out(trees["cje"], "rev-parse", "HEAD")
    subs = git_out(trees["cje"], "submodule", "status", "--recursive")
    lines = [
        "SSInger %s (%s): corresponding source" % (version, platform),
        "",
        "These binaries are built with JUCE under its AGPLv3 option, so each of",
        "them (VST3, CLAP, Standalone, and AU or LV2 where present) is licensed",
        "under the GNU Affero General Public License, version 3, as a whole:",
        "licenses/AGPL-3.0.txt.  SSInger's own source is MIT (LICENSE).",
        "",
        "The complete corresponding source of these binaries is offered at no",
        "charge from the same place as the binaries, for as long as they are",
        "offered:",
        "",
        "  Repository:  %s" % repo_url,
        "  Commit:      %s" % commit,
        "  Snapshot:    %s/archive/%s.tar.gz" % (repo_url, commit),
        "  Folders:     SSInger/ (the plug-in) and src/csrc/ (the SSI-263 engine",
        "               it links), plus third_party/casso/LICENSE",
        "",
        "Fetched by CMake at configure time, as pinned in SSInger/CMakeLists.txt:",
        "",
        "  JUCE                  https://github.com/juce-framework/JUCE",
        "                        tag %s%s" % (juce_tag, (" (commit %s)" % juce_head) if juce_head else ""),
        "  clap-juce-extensions  https://github.com/free-audio/clap-juce-extensions",
        "                        commit %s" % (cje_head or cje_commit),
        "                        with its submodules at the commits it pins:",
    ]
    if subs:
        lines += ["                          " + s.strip() for s in subs.splitlines()]
    else:
        lines += ["                          clap-libs/clap, clap-libs/clap-helpers"]
    lines += [
        "",
        "How to build: BUILD.md in this folder (\"Build (from the repo root)\"),",
        "the same steps CI runs in .github/workflows/ssinger.yml at that commit.",
        "For example, on Linux:",
        "",
        "  cmake -S SSInger -B build -G Ninja -DCMAKE_BUILD_TYPE=Release",
        "  cmake --build build --target SSInger_VST3 SSInger_CLAP SSInger_LV2 SSInger_Standalone",
        "",
    ]
    if run_url:
        lines += ["Built by: %s" % run_url, ""]
    return "\n".join(lines)


def notices_index(platform, spdx, trees):
    _juce_tag, cje_commit = pins()
    pk = {p["SPDXID"]: p for p in spdx["packages"]}
    out = [
        "Third-party pieces in these SSInger binaries, and where their notices are",
        "",
        "SSInger's own code and the SSI-263 engine: MIT (../LICENSE).",
        "The engine draws on Casso (MIT): Casso-MIT.txt.",
        "The binaries as a whole: GNU AGPLv3 (AGPL-3.0.txt), from JUCE's AGPLv3 option.",
        "README.md here is SSInger/third_party/README.md: what each piece is and why.",
        "",
    ]
    for folder, spdx_id, lic, _plats, files in pieces_for(platform):
        p = pk.get(spdx_id) if spdx_id else None
        if p:
            ver = p.get("versionInfo", "")
        elif folder == "clap-juce-extensions":
            ver = "commit " + (git_out(trees["cje"], "rev-parse", "HEAD") or cje_commit)
        else:
            ver = "as pinned by clap-juce-extensions"
        if ver == "NOASSERTION":
            ver = "unversioned"
        shipped = [piece_dest(folder, path) for tree, path in files
                   if os.path.isfile(os.path.join(trees[tree.rstrip("?")], path))]
        if not files:
            shipped = ["licenses/%s/NOTICE.txt" % folder]
        out.append("%-22s %s; %s" % (folder, lic, ver))
        for s in shipped:
            out.append("%-22s   %s" % ("", s[len("licenses/"):]))
    out += [
        "",
        "Not compiled in: JUCE's ASIO support (off), the AAX SDK (no AAX target),",
        "Oboe (Android only), the web browser and curl (off), juce_javascript,",
        "juce_animation, juce_opengl, juce_box2d.",
        "The full SPDX inventory of everything JUCE bundles: JUCE/JUCE.spdx.json.",
        "",
    ]
    return "\n".join(out)


def assemble(args):
    version = args.version or release_version()
    stem, name = archive_name(version, args.platform)
    trees = {"juce": os.path.join(args.deps, "juce-src"),
             "cje": os.path.join(args.deps, "clap_juce_extensions-src")}
    stage_root = os.path.join(args.stage, stem)
    if os.path.exists(stage_root):
        shutil.rmtree(stage_root)
    os.makedirs(stage_root)

    for fmt, bundle, _inner in BUNDLES[args.platform]:
        src = os.path.join(args.artefacts, fmt, bundle)
        if not os.path.exists(src):
            sys.exit("missing bundle: %s" % src)
        copy_bundle(src, os.path.join(stage_root, fmt, bundle))

    for dest, src in OWN_FILES.items():
        os.makedirs(os.path.dirname(os.path.join(stage_root, dest)), exist_ok=True)
        shutil.copyfile(src, os.path.join(stage_root, dest))

    spdx_path = os.path.join(trees["juce"], "JUCE.spdx.json")
    spdx = json.load(open(spdx_path, encoding="utf-8"))
    extracted = {e["licenseId"]: e for e in spdx.get("hasExtractedLicensingInfos", [])}
    pk = {p["SPDXID"]: p for p in spdx["packages"]}
    for folder, spdx_id, lic, _plats, files in pieces_for(args.platform):
        if not files:
            p = pk[spdx_id]
            text = ["%s %s, licence %s (from JUCE.spdx.json)" % (p["name"], p.get("versionInfo", ""),
                                                                 p["licenseConcluded"]),
                    "Supplier: %s" % p.get("supplier", ""),
                    "Source: %s" % p.get("sourceInfo", ""), ""]
            ref = extracted.get(p["licenseConcluded"])
            if ref:
                text += [ref.get("name", ""), "", ref.get("extractedText", "")]
            write(os.path.join(stage_root, "licenses", folder, "NOTICE.txt"), "\n".join(text) + "\n")
            continue
        for tree, path in files:
            optional = tree.endswith("?")
            src = os.path.join(trees[tree.rstrip("?")], path)
            if not os.path.isfile(src):
                if optional:
                    continue
                sys.exit("missing licence file: %s" % src)
            dst = os.path.join(stage_root, piece_dest(folder, path))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)

    write(os.path.join(stage_root, NOTICES_INDEX), notices_index(args.platform, spdx, trees))
    write(os.path.join(stage_root, "SOURCE.txt"),
          source_txt(version, args.platform, args.repo_url, args.commit, args.run_url, trees))

    os.makedirs(args.out, exist_ok=True)
    out = os.path.join(args.out, name)
    if os.path.exists(out):
        os.remove(out)
    pack(stage_root, out)
    print(out)


def _tar_filter(info):
    info.uid = info.gid = 0
    info.uname = info.gname = "root"
    return info


def pack(stage_root, out):
    parent, stem = os.path.split(stage_root)
    if out.endswith(".tar.gz"):
        with tarfile.open(out, "w:gz") as t:
            t.add(stage_root, arcname=stem, filter=_tar_filter)
    elif sys.platform == "darwin":
        subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", stage_root, out], check=True)
    else:
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
            for dirpath, dirnames, filenames in os.walk(stage_root):
                dirnames.sort()
                rel = os.path.relpath(dirpath, parent).replace(os.sep, "/")
                z.writestr(zipfile.ZipInfo(rel + "/"), "")
                for fn in sorted(filenames):
                    full = os.path.join(dirpath, fn)
                    z.write(full, rel + "/" + fn)


def read_archive(path):
    """[(name, is_dir, mode or None)], and a reader for one member's bytes."""
    if path.endswith(".tar.gz"):
        t = tarfile.open(path, "r:gz")
        entries = [(m.name.rstrip("/"), m.isdir(), m.mode) for m in t.getmembers()]
        return entries, lambda n: t.extractfile(n).read()
    z = zipfile.ZipFile(path)
    entries = []
    for i in z.infolist():
        mode = (i.external_attr >> 16) or None
        entries.append((i.filename.rstrip("/"), i.filename.endswith("/"), mode))
    return entries, lambda n: z.read(n)


def check(args):
    name = os.path.basename(args.archive)
    stem = re.sub(r"\.(zip|tar\.gz)$", "", name)
    entries, read = read_archive(args.archive)
    names = [e[0] for e in entries if not e[0].startswith("__MACOSX")]
    errors = []
    bad = [n for n in names if "\\" in n]
    if bad:
        errors.append("backslash in %d entry names, e.g. %s" % (len(bad), bad[0]))
    paths = set()
    for n in names:
        parts = n.split("/")
        for i in range(1, len(parts) + 1):
            paths.add("/".join(parts[:i]))
    tops = {n.split("/")[0] for n in names}
    if tops != {stem}:
        errors.append("top level is %s, expected only %s" % (sorted(tops), stem))

    want = list(OWN_FILES) + ["SOURCE.txt", NOTICES_INDEX]
    for folder, _id, _lic, _plats, files in pieces_for(args.platform):
        if not files:
            want.append("licenses/%s/NOTICE.txt" % folder)
        want += [piece_dest(folder, path) for tree, path in files if not tree.endswith("?")]
    for fmt, bundle, inner in BUNDLES[args.platform]:
        want.append("%s/%s" % (fmt, bundle))
        if inner:
            want.append("%s/%s/%s" % (fmt, bundle, inner))
    for w in want:
        if "%s/%s" % (stem, w) not in paths:
            errors.append("missing: " + w)

    if "%s/SOURCE.txt" % stem in names:
        src = read("%s/SOURCE.txt" % stem).decode("utf-8")
        if args.commit not in src:
            errors.append("SOURCE.txt does not name commit " + args.commit)
    if args.platform.startswith("linux"):
        modes = {e[0]: e[2] for e in entries}
        m = modes.get("%s/Standalone/SSInger" % stem)
        if m is not None and not (m & stat.S_IXUSR):
            errors.append("Standalone/SSInger is not executable in the archive")

    print("%s: %d entries" % (name, len(names)))
    listing = sorted(p for p in paths if p.count("/") <= 2 or p.startswith(stem + "/licenses/"))
    for p in listing:
        print("  " + p)
    for e in errors:
        print("ERROR: " + e)
    if errors:
        sys.exit(1)
    print("OK")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("version")
    b = sub.add_parser("build")
    b.add_argument("--platform", required=True, choices=PLATFORMS)
    b.add_argument("--artefacts", required=True)
    b.add_argument("--deps", required=True, help="CMake's _deps folder (juce-src, clap_juce_extensions-src)")
    b.add_argument("--out", required=True)
    b.add_argument("--stage", default=os.path.join("build", "release-stage"))
    b.add_argument("--repo-url", required=True)
    b.add_argument("--commit", required=True)
    b.add_argument("--run-url", default="")
    b.add_argument("--version", default="")
    c = sub.add_parser("check")
    c.add_argument("archive")
    c.add_argument("--platform", required=True, choices=PLATFORMS)
    c.add_argument("--commit", required=True)
    args = ap.parse_args()
    if args.cmd == "version":
        print(release_version())
    elif args.cmd == "build":
        assemble(args)
    else:
        check(args)


if __name__ == "__main__":
    main()
