"""Generate from bl_languages.txt (one row per Braille Lite firmware image; the columns are described at the top of
the text file):
  bl_languages_table.h    the releases bl_firmware.c knows
  ../voices_blazie.h      the voices the releases with a voice key give (blazie:blazie_hu ...): ssv_info rows that
                          voices.c lists after its hand-written ones, their files under blazie/bl2000/ in the firmware
                          folder (the image as <file>, its state as <release id>.state, made by its own recipe)

    python gen_languages.py           # rewrite the headers
    python gen_languages.py --check   # exit 1 if one is stale or the table is malformed (run_tests)
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "bl_languages.txt")
OUT = os.path.join(HERE, "bl_languages_table.h")
OUT_VOICES = os.path.join(HERE, "..", "voices_blazie.h")
FIRST_SLOT = 9                  # voices.c's hand-written voices use slots 0..8

MODELS = {"bl": "BL_MODEL_BRAILLE_LITE", "bns": "BL_MODEL_BNS2000", "bns640": "BL_MODEL_BNS640"}
VOICES = {"english": "BLV_FW_ENGLISH", "spanish": "BLV_FW_SPANISH", "german": "BLV_FW_GERMAN",
          "italian": "BLV_FW_ITALIAN", "slovak": "BLV_FW_SLOVAK", "-": "BLV_FW_NONE"}
SHAPES = {"english": "BLV_SHAPE_ENGLISH", "slow": "BLV_SHAPE_SLOW"}
CODE_PAGES = {"850": "850", "852": "852", "-": "0"}
TEXTS = {"7bit": "BLV_LATIN1", "850": "BLV_CP850", "852": "BLV_CP852", "857": "BLV_CP857", "860": "BLV_CP860",
         "866": "BLV_CP866", "hun": "BLV_CPHUN",       # hun: the Hungarian unit's spoken letters (bl_cphun.h)
         "852sk": "BLV_CP852SK",                       # 852sk: 852, o circumflex sent as its capital
         "852pl": "BLV_CP852PL", "852hr": "BLV_CP852HR", "850fr": "BLV_CP850FR",   # their folds (bl_voice.c)
         "850dano": "BLV_CP850DANO", "850sv": "BLV_CP850SV",                     # their own letters only
         "850es": "BLV_CP850ES", "850it": "BLV_CP850IT", "857tr": "BLV_CP857TR",   # their folds and drops
         "860pt": "BLV_CP860PT", "866ru": "BLV_CP866RU",
         "864ar": "BLV_CP864AR",                       # the Arabic (UAE) unit's 27 letters (bl_cp864ar.h)
         "-": "BLV_LATIN1"}                             # not yet read: plain letters, as the English unit's
BOOTS = {"fast": "0", "slow": "1"}
TRILLIONS = {"fold": "BLV_TRILLIONS_FOLD", "reads": "BLV_TRILLIONS_READS", "digits": "BLV_TRILLIONS_DIGITS",
             "-": "BLV_TRILLIONS_UNKNOWN"}
YES_NO = {"yes": "1", "no": "0"}
MARKS = {"own": "1", "no": "0", "-": "0"}
COMMAS = {"ok": "BLV_COMMAS_OK", "repeat": "BLV_COMMAS_REPEAT", "-": "BLV_COMMAS_UNKNOWN"}
# a-z in computer braille as port 40h bits (dot 1 = bit 0 .. dot 6 = bit 5): the chord the unit reads as the letter
LETTERS = (0x01, 0x03, 0x09, 0x19, 0x11, 0x0B, 0x1B, 0x13, 0x0A, 0x1A, 0x05, 0x07, 0x0D, 0x1D, 0x15, 0x0F, 0x1F,
           0x17, 0x0E, 0x1E, 0x25, 0x27, 0x3A, 0x2D, 0x3D, 0x35)
SHA = re.compile(r"^[0-9a-f]{64}$")


def rows():
    out, problems = [], []
    for n, line in enumerate(open(SRC, encoding="utf-8"), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        f = [x.strip() for x in line.split("|")]
        if len(f) != 21:
            problems.append("line %d: %d columns, want 21" % (n, len(f)))
            continue
        rid, model, tag, voice, shape, yes, cp, text, boot, echo, speaks, trillions, marks, commas, length, image, \
            state, \
            name, vname, file, label = f
        bad = [what for what, ok in (
            ("id", re.match(r"^[a-z0-9_]+$", rid)), ("model", model in MODELS),
            ("tag", re.match(r"^[a-z]{2,3}(-[A-Za-z0-9]+)*$", tag)),
            ("voice", voice in VOICES or re.match(r"^blazie_[a-z_]+$", voice)),
            ("shape", shape in SHAPES), ("yes", re.match(r"^[a-z]$", yes)), ("cp", cp in CODE_PAGES),
            ("text", text in TEXTS), ("boot", boot in BOOTS), ("echo", echo in YES_NO), ("speaks", speaks in YES_NO),
            ("trillions", trillions in TRILLIONS), ("marks", marks in MARKS and (marks != "own" or echo == "yes")),
            ("commas", commas in COMMAS),
            ("length", length.isdigit()), ("image", SHA.match(image)), ("state", SHA.match(state)),
            ("name", name and '"' not in name and "\\" not in name),
            ("vname", vname and '"' not in vname and "\\" not in vname),
            ("file", re.match(r"^[A-Za-z0-9_]+\.BNS$", file)),
            ("label", label and '"' not in label and "\\" not in label)) if not ok]
        if bad:
            problems.append("line %d: bad %s" % (n, ", ".join(bad)))
            continue
        out.append(dict(id=rid, model=model, tag=tag, voice=voice, shape=shape, yes=yes, cp=cp, text=text, boot=boot,
                        echo=echo, speaks=speaks, trillions=trillions, marks=marks, commas=commas,
                        length=int(length), image=image, state=state, name=name, vname=vname, file=file,
                        label=label))
    for r in out:                        # a new voice: named, speaking, a Braille Lite
        if r["voice"].startswith("blazie_") and (r["vname"] == "-" or r["speaks"] != "yes" or r["model"] != "bl"):
            problems.append("%s: voice %s wants a vname, speaks = yes and model bl" % (r["id"], r["voice"]))
    for r in out:                        # a voice's numbers past twelve digits: measured, never a guess
        if r["voice"] != "-" and r["trillions"] == "-":
            problems.append("%s: voice %s wants its trillions measured (fold, reads or digits)" % (r["id"], r["voice"]))
    keys = [r["voice"] for r in out if r["voice"].startswith("blazie_")]
    for k in set(keys):                  # one release per new voice (a user's setting names the voice, not it)
        if keys.count(k) > 1:
            problems.append("voice %s given by %d releases" % (k, keys.count(k)))
    for key in ("id", "image", "name"):
        seen = set()
        for r in out:
            if r[key] in seen:
                problems.append("%s %s listed twice" % (key, r[key]))
            seen.add(r[key])
    return out, problems


def c_string(s):
    r"""A C string literal of s: UTF-8 bytes past ASCII as \xNN escapes, the string split only where a hex digit
    follows an escape (it would run on)."""
    out, after_escape = ["\""], False
    for b in s.encode("utf-8"):
        if b >= 0x80:
            out.append("\\x%02x" % b)
            after_escape = True
            continue
        ch = chr(b)
        if after_escape and ch in "0123456789abcdefABCDEF":
            out.append("\" \"")
        out.append(ch)
        after_escape = False
    out.append("\"")
    return "".join(out)


def render(table):
    lines = ["/* Generated by gen_languages.py from bl_languages.txt: do not edit. */",
             "#ifndef BL_LANGUAGES_TABLE_H", "#define BL_LANGUAGES_TABLE_H", "",
             "/* id, model, tag, voice, voice key, shape, yes chord, code page, text encoding, boot, echo, speaks,",
             "   trillions, marks, commas, image length, image sha256, state sha256, name, voice name, file, label */",
             "static const bl_release KNOWN[] = {"]
    for r in table:
        lines.append('    {"%s", %s, "%s", %s, %s, %s, 0x%02X,' % (
            r["id"], MODELS[r["model"]], r["tag"], VOICES.get(r["voice"], "BLV_FW_NONE"),
            '"%s"' % r["voice"] if r["voice"].startswith("blazie_") else "NULL", SHAPES[r["shape"]],
            LETTERS[ord(r["yes"]) - ord("a")]))
        lines.append('     %s, %s, %s, %s, %s, %s, %s, %s, %d,' % (
            CODE_PAGES[r["cp"]], TEXTS[r["text"]], BOOTS[r["boot"]], YES_NO[r["echo"]], YES_NO[r["speaks"]],
            TRILLIONS[r["trillions"]], MARKS[r["marks"]], COMMAS[r["commas"]], r["length"]))
        lines.append('     "%s",' % r["image"])
        lines.append('     "%s",' % r["state"])
        lines.append("     %s, %s, \"%s\"," % (c_string(r["name"]),
                                             "NULL" if r["vname"] == "-" else c_string(r["vname"]), r["file"]))
        lines.append("     %s}," % c_string(r["label"]))
    lines += ["};", "", "#endif", ""]
    return "\n".join(lines)


def render_voices(table):
    new = [r for r in table if r["voice"].startswith("blazie_")]
    lines = ["/* Generated by gen_languages.py from blazie/bl_languages.txt: do not edit. */",
             "#ifndef VOICES_BLAZIE_H", "#define VOICES_BLAZIE_H", "",
             "/* the Braille Lite voices the releases give (voices.c lists them after its own): id, name, language,",
             "   engine, slot (each its own unit), files under blazie/bl2000/ (the image, its state) */",
             "#define SSV_BLAZIE_RELEASE_VOICES %d" % len(new),
             "#define SSV_BLAZIE_RELEASE_ROWS \\"]
    for i, r in enumerate(new):
        lines.append('    {"blazie:%s", \\' % r["voice"])
        lines.append('     %s, \\' % c_string(r["vname"]))
        lines.append('     "%s", SSV_BLAZIE, %d, \\' % (r["tag"], FIRST_SLOT + i))
        lines.append('     {"blazie/bl2000/%s", "blazie/bl2000/%s.state", NULL}},%s' % (
            r["file"], r["id"], " \\" if i + 1 < len(new) else ""))
    # the same voices for speech-dispatcher's module (src/platforms/speechd/sd_voices.c): its voice_def rows, the
    # files relative to its data folder
    lines += ["", "/* sd_voices.c's rows for them: id, name, language, engine, files in the module's data folder */",
              "#define SSV_BLAZIE_RELEASE_SD_ROWS \\"]
    for i, r in enumerate(new):
        lines.append('    {"blazie:%s", %s, "%s", "braille-lite", \\' % (r["voice"], c_string(r["vname"]), r["tag"]))
        lines.append('     {"bl2000/%s", "bl2000/%s.state", NULL}},%s' % (
            r["file"], r["id"], " \\" if i + 1 < len(new) else ""))
    lines += ["", "#endif", ""]
    return "\n".join(lines)


def main():
    table, problems = rows()
    for p in problems:
        print("bl_languages.txt: " + p)
    if problems:
        sys.exit(1)
    stale = 0
    for out, text in ((OUT, render(table)), (OUT_VOICES, render_voices(table))):
        old = open(out, encoding="utf-8").read() if os.path.isfile(out) else None
        name = os.path.basename(out)
        if "--check" in sys.argv:
            if old != text:
                print("%s is stale: run python src/csrc/blazie/gen_languages.py" % name)
                stale = 1
            else:
                print("%s matches bl_languages.txt" % name)
            continue
        with open(out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("wrote %s" % os.path.normpath(out))
    if stale:
        sys.exit(1)
    if "--check" in sys.argv:
        print("%d releases, %d voices" % (len(table), sum(r["voice"].startswith("blazie_") for r in table)))


if __name__ == "__main__":
    main()
