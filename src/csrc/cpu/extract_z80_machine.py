"""Generate z80_mame_machine.cpp from MAME's Z80 core (src/devices/cpu/z80): the class's members, the flag and ALU
helpers, the power-on values, reset, the input lines and the instructions, with the framework replaced by named
substitutions (each listed below and marked 'CHANGED' in the output).  It refuses to run if a source is not the pinned
revision or an anchor line has moved.

    python src/csrc/cpu/extract_z80_machine.py <folder with the four pinned files> <folder with the 2024 z80.cpp>

The pinned sources: MAME 1c924ea79551fdfc3960bfa8ceb93fc1928c661b, src/devices/cpu/z80/ (sha256 of each file with LF
line ends below; mame_z80/PINNED.txt).  BSD-3-Clause, copyright-holders: Juergen Buchmueller (z80.cpp, z80.h), Juergen
Buchmueller and Andrei I. Holub (z80.lst), Andre I. Holub (z80make.py, as its header spells him); the generated file
keeps those notices.  And ONE more file from an older revision, for the short names alone (below): z80.cpp at MAME
589e5219966d3303a9ea3116bd9c56707db559ef, BSD-3-Clause, copyright-holders: Juergen Buchmueller.

MAME's Z80 is itself generated: z80.lst describes every instruction step by step, and MAME's build runs z80make.py on
it to make z80.hxx, the whole body of execute_run: one loop that fetches, dispatches, and can suspend after any step.
So there is no finished upstream file to take the instructions' line ranges from.  This script makes z80.hxx as MAME's
build does (scripts/src/cpu.lua at the pinned revision: `python z80make.py z80.lst z80.hxx`, no type argument: the
plain Z80): it runs the PINNED generator on the PINNED .lst (the bytes whose sha256 it has just checked, copied to a
scratch folder; python -I, from an empty folder), checks the sha256 of the file the generator wrote, and puts that
text in place of execute_run's #include.  The rest is exact line ranges, as for the other MAME cores.

The short names.  z80.lst and z80.cpp are written in short macros for the registers and the flag bits (PC, A, HL,
CF, TDAT8 ...).  At the pinned revision upstream keeps their definitions in a fifth file, z80.inc, which carries NO
licence header: it is not taken, and this script refuses to run on a folder that holds one, so that it cannot be read
by mistake.  Upstream last published those definitions under a BSD-3-Clause header in z80.cpp at 589e521 (2024-06-12,
the parent of the commit that moved them out): the script copies them from THAT file, lines 128-206, and brings them
to the pinned z80.h's members by named substitutions, each checked to match exactly once (DEFINES_SUBS below).  It
then requires the result to define exactly the names it expects, no more and no fewer.

Left out: the device framework (constructors and the device type, address spaces, the state table, save states, the
disassembler, the daisy chain) -- our step driver in z80_mame.cpp drives execute_run (CONTRACT.md 15).  The shim the
output compiles against is z80_mame.hpp.
"""
import hashlib
import os
import subprocess
import sys
import tempfile

REVISION = "1c924ea79551fdfc3960bfa8ceb93fc1928c661b"
SHA256 = {   # of the file with LF line ends (as raw.githubusercontent.com serves it)
    "z80.cpp": "138bd48f9f6cd12e3422383e28abe4f73e39610b4d5db54064e29f4bfaadbb73",
    "z80.h": "4e8d683130d3d55a8a605cdf2295a838d45de13144870884eb40266ecb5dbbbc",
    "z80.lst": "9df749e9fab1493334fb1460307556debfb20ed5f0f10311c914371267406828",
    "z80make.py": "78158bd3fef60263ca547eabd7a8a167e8e7464431b3e44f56b38d4e90b3ed54",
}
# what the pinned z80make.py writes for the pinned z80.lst (MAME's z80.hxx), with LF line ends
HXX_SHA256 = "3357bac2c54cc851c0d0c804e2008e26f7f71327ef08f4100dc400beca39fa89"
HXX_INCLUDE = '\t#include "cpu/z80/z80.hxx"\n'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "z80_mame_machine.cpp")
NOT_TAKEN = "z80.inc"        # no licence header: never an input, and no folder given to this script may hold it

# ---- the short names: z80.cpp at the 2024 revision that still held them under its BSD-3-Clause header ---------------
DEFINES_REVISION = "589e5219966d3303a9ea3116bd9c56707db559ef"
DEFINES_SHA256 = "b534683ccb8a13e8970fc5a27ffcf766e404067d26326fd1b410315092a69343"   # z80.cpp there, LF line ends
DEFINES_FIRST, DEFINES_LAST = 128, 206
DEFINES_OPENER = "/* On an NMOS Z80, if LD A,I or LD A,R is interrupted, P/V flag gets reset,"
DEFINES_CLOSER = "#define TDAT8    m_shared_data.b.l"
# (the 2024 line, the line for the pinned z80.h, why) -- each must match exactly once
DEFINES_SUBS = [
    # a register was a 32-bit PAIR then, its 16 bits .w.l; the pinned z80.h declares each a PAIR16, its 16 bits .w
    ("#define %-8s%s.w.l\n" % (name, member),
     "#define %-8s%s.w   // CHANGED: %s.w.l (a PAIR16 at the pinned z80.h)\n" % (name, member, member),
     "%s: the register's 16 bits" % name)
    for name, member in (("PC", "m_pc"), ("SP", "m_sp"), ("AF", "m_af"), ("BC", "m_bc"), ("DE", "m_de"),
                         ("HL", "m_hl"), ("IX", "m_ix"), ("IY", "m_iy"), ("WZ", "m_wz"))
] + [
    ("#define PRVPC   m_prvpc.d     // previous program counter\n",
     "#define PRVPC   m_prvpc.w     // previous program counter   CHANGED: m_prvpc.d (a PAIR16 at the pinned z80.h)\n",
     "PRVPC: the register's 16 bits"),
    # the SCF/CCF latch and its next value were members of the class; the pinned z80.h keeps them inside m_f
    ("#define Q       m_q\n", "#define Q       m_f.q   // CHANGED: m_q (inside m_f at the pinned z80.h)\n",
     "Q: the latch's place"),
    ("#define QT      m_qtemp\n", "#define QT      m_f.qtemp   // CHANGED: m_qtemp (inside m_f at the pinned z80.h)\n",
     "QT: the latch's place"),
    # the one name the pinned z80.lst uses that the 2024 file does not have: bits 5 and 3 of F together
    ("#define SF      0x80\n",
     "#define SF      0x80\n"
     "#define YXF     (YF | XF)   // CHANGED (ours): not in this file.  The pinned z80.lst's name for the two bits\n"
     "                            // of F that Zilog leaves unused, 5 and 3 together: 28h\n",
     "YXF added"),
] + [
    # the 32-bit views: a PAIR16 has none, and nothing at the pinned revision uses them
    ("#define %-8s%s.d\n" % (name, member), "// CHANGED: %s dropped (%s.d: a PAIR16 has no 32-bit view)\n" % (name, member),
     "%s dropped" % name)
    for name, member in (("PCD", "m_pc"), ("SPD", "m_sp"), ("AFD", "m_af"), ("BCD", "m_bc"), ("DED", "m_de"),
                         ("HLD", "m_hl"), ("IXD", "m_ix"), ("IYD", "m_iy"))
] + [
    # the shared address register: gone from the pinned z80.h (upstream's 700238bf8d7), and from z80.lst with it
    ("#define TADR     m_shared_addr.w   // Typically represents values from A0..15 pins. 16bit input in steps.\n",
     "// CHANGED: TADR dropped (m_shared_addr is not in the pinned z80.h)\n", "TADR dropped"),
    ("#define TADR_H   m_shared_addr.b.h\n", "// CHANGED: TADR_H dropped (likewise)\n", "TADR_H dropped"),
    ("#define TADR_L   m_shared_addr.b.l\n", "// CHANGED: TADR_L dropped (likewise)\n", "TADR_L dropped"),
]
# what the block must define when they are done: exactly these, each once.  The first 43 are the names the pinned
# z80.cpp and z80.lst are written in; XF and YF (YXF is made of them), INT_IRQ and NMI_IRQ come with the lines copied
# and nothing uses them
DEFINES_NAMES = (
    "HAS_LDAIR_QUIRK CF NF PF VF HF YXF ZF SF PRVPC PC SP AF A F Q QT I R R2 BC B C DE D E HL H L IX HX LX IY HY LY "
    "WZ WZ_H WZ_L TDAT TDAT2 TDAT_H TDAT_L TDAT8 XF YF INT_IRQ NMI_IRQ").split()

# The class's head, ours: in place of z80.h lines 30-79 (the framework's bases, the configuration and the overrides).
# z80.h's own members follow it, from line 81; the address spaces and the callbacks (lines 128-141) are the shim's.
CLASS_HEAD = [
    "// ---- ours: the class's head, in place of z80.h lines 30-79 (the framework's bases and overrides) --------------",
    "class z80_device : public z80_shim   // CHANGED: no cpu_device or z80_daisy_chain_interface base: z80_mame.hpp's shim",
    "{",
    "public:",
    "\t// the bus cycles' lengths are upstream's constructor's (z80.cpp lines 921-923): M1 4, memory 3, I/O 4",
    "\texplicit z80_device(const cpu_bus *bus)",
    "\t\t: z80_shim(bus), m_icount(0), m_tmp_irq_vector(0), m_ref(0xffff00), m_m1_cycles(4), m_mreq_cycles(3),",
    "\t\t  m_iorq_cycles(4)",
    "\t{",
    "\t\tm_shared_data.w = m_shared_data2.w = 0;",
    "\t}",
    "",
    "\t// our step driver (z80_mame.cpp)",
    "\tint drv_pass(bool accept, bool enter);    // execute_run for one acceptance, or one instruction or HALT slot",
    "",
    "\t// z80.h lines 56-67, the few kept (no longer virtual)",
    "\tvoid init_state();                        // device_start()'s first values (the generated file)",
    "\tvoid device_reset();",
    "\tvoid execute_run();",
    "\tvoid execute_set_input(int inputnum, int state);",
    "",
]

# (file, first line, last line, the text that must open the range, what it is); lines as sed and grep count them
RANGES = [
    ("z80.h", 10, 16, "enum", "the input lines' numbers"),
    ("CLASS_HEAD", 0, 0, "", ""),
    ("z80.h", 81, 126, "\tvoid illegal_1();", "the helpers and the bus accesses"),
    ("z80.h", 143, 217, "\tstatic constexpr u8 SA_BUSREQ",
     "the service bits, the registers, the flags, the internal state"),
    ("z80.cpp", 39, 625, "/***************************************************************",
     "the flags, HALT, the bus accesses, the ALU, EI"),
    ("z80.cpp", 681, 713, "\t// Reset registers to their initial values", "the power-on values (device_start's)"),
    ("z80.cpp", 720, 721, "\tIX = IY = 0xffff;", "the power-on values, the end"),
    ("z80.cpp", 759, 778, "/****************************************************************************", "reset"),
    ("z80.cpp", 780, 786, "/****************************************************************************",
     "execute_run: the generated loop"),
    ("z80.cpp", 788, 825, "void z80_device::execute_set_input(int inputnum, int state)", "the input lines"),
]

# before and after these ranges, lines of our own
BEFORE = {
    ("z80.cpp", 681, 713): [
        "void z80_device::init_state()   // CHANGED (ours): device_start()'s values only; upstream's device_start",
        "{                                // sets the framework up before, between and after them",
    ],
}
AFTER = {
    ("z80.h", 143, 217): ["};   // (ours) the class ends: z80.h line 218"],
    ("z80.cpp", 720, 721): ["}   // CHANGED (ours): closes init_state()"],
}

# The instruction-fetch state (z80.lst's `ffff`) is in the generator's output twice: in the loop that resumes a
# suspended instruction (its steps as `case` labels, one tab deeper) and in the fast loop.  Each change of it is made
# in both, so the state means the same wherever it is entered: {i} is the copy's indentation.
FETCH_COPIES = ("\t\t\t", "\t\t")

FETCH_TOP = ("{i}if (m_icount <= 0) {{\n{i}\tm_ref = 0xffff00;\n{i}\treturn;\n{i}}}\n{i}if (m_service_attention) {{\n")
FETCH_TOP_NEW = (
    "{i}// CHANGED: a pass is ONE acceptance, or ONE instruction or HALT slot (z80_mame.cpp, a step's A and E).\n"
    "{i}// Upstream's loop comes back here after each and goes on while its time slice lasts; here the driver lets a\n"
    "{i}// pass in once (m_drv_enter) and the second arrival ends it, at the instruction's boundary\n"
    "{i}if (m_icount <= 0 || !m_drv_enter) {{\n{i}\tm_ref = 0xffff00;\n{i}\treturn;\n{i}}}\n"
    "{i}m_drv_enter = false;\n{i}if (m_service_attention) {{\n")

SAMPLE = "{i}if (get_service_attention<SA_NMI_PENDING>()) {{\n"
SAMPLE_NEW = (
    "{i}if (m_drv_accept) {{   // CHANGED: the lines are looked at in a step's acceptance pass only (z80_mame.cpp,\n"
    "{i}// phase A); the instruction's own pass (E) comes through here without looking, so a line the board raises at\n"
    "{i}// the boundary (D) is taken at the NEXT step's A (CONTRACT.md 1)\n"
    + SAMPLE)

SAMPLE_END = "{i}}}\n{i}\tset_service_attention<SA_AFTER_EI, 0>();\n"
SAMPLE_END_NEW = (
    "{i}}}\n"
    "{i}}}   // CHANGED: closes if (m_drv_accept)\n"
    "{i}if (m_drv_accept) {{   // CHANGED: the acceptance pass ends here, taken or not: its charge and the boundary\n"
    "{i}\t// come before the instruction (CONTRACT.md 1, B and D).  Upstream runs on into the fetch below\n"
    "{i}\tm_ref = 0xffff00;\n{i}\treturn;\n"
    "{i}}}\n"
    "{i}\tset_service_attention<SA_AFTER_EI, 0>();\n")

IRQFETCH = (      # the block's own braces are at two tabs in both copies
    "\t\t{{\n{i}// fetch the IRQ vector\n{i}device_z80daisy_interface *intf = daisy_get_irq_device();\n"
    "{i}if (intf != nullptr) {{\n{i}\tm_tmp_irq_vector = intf->z80daisy_irq_ack();\n{i}\tif (debugger_enabled())\n"
    "{i}\t\tdevice_execute_interface::device().debug()->interrupt_hook(0, m_pc.w);\n{i}}} else {{\n"
    "{i}\tm_tmp_irq_vector = standard_irq_callback(0, m_pc.w);\n{i}}}\n"
    "{i}LOGMASKED(LOG_INT, \"single INT m_tmp_irq_vector $%02x\\n\", m_tmp_irq_vector);\n\t\t}}\n")
IRQFETCH_NEW = (
    "{i}// CHANGED: the acknowledge cycle's byte(s) from cpu_bus.irq_ack (z80_mame.hpp); upstream asks its daisy chain\n"
    "{i}// or the framework's callback\n"
    "{i}m_tmp_irq_vector = drv_irq_ack(m_im);\n")

# (upstream text, replacement, why, how many times it must match) -- applied to the copied text and to the generator's
SUBS = [
    ("\tvirtual u8 data_read(u16 addr);\n\tvirtual void data_write(u16 addr, u8 value);\n"
     "\tvirtual u8 stack_read(u16 addr) { return data_read(addr); }\n"
     "\tvirtual void stack_write(u16 addr, u8 value) { return data_write(addr, value); }\n"
     "\tvirtual u8 opcode_read();\n\tvirtual u8 arg_read();\n",
     "\t// CHANGED: not virtual (the plain Z80 only; upstream's derived CPUs override them)\n"
     "\tu8 data_read(u16 addr);\n\tvoid data_write(u16 addr, u8 value);\n"
     "\tu8 stack_read(u16 addr) { return data_read(addr); }\n"
     "\tvoid stack_write(u16 addr, u8 value) { return data_write(addr, value); }\n"
     "\tu8 opcode_read();\n\tu8 arg_read();\n",
     "the bus accesses not virtual", 1),
    # NMI: upstream latches an edge only once its machine's clock has run, so that a line asserted in the very
    # instant its RESET is released is not taken.  Here reset and the lines are separate calls of the board
    ("\t\tif (m_nmi_state == CLEAR_LINE && state != CLEAR_LINE && total_cycles())\n",
     "\t\tif (m_nmi_state == CLEAR_LINE && state != CLEAR_LINE)   // CHANGED: an edge is latched whenever it comes,\n"
     "\t\t\t// before the first step too; upstream also wanted total_cycles() nonzero (its machine's time 0)\n",
     "an NMI edge before the first step", 1),
]
for _i in FETCH_COPIES:      # each anchor from its line's start: the shallower copy's is not inside the deeper one's
    SUBS += [("\n" + _old.format(i=_i), "\n" + _new.format(i=_i), _why, 1) for _old, _new, _why in (
        (FETCH_TOP, FETCH_TOP_NEW, "a pass ends at the instruction's boundary"),
        (SAMPLE, SAMPLE_NEW, "the lines sampled at A only"),
        (SAMPLE_END, SAMPLE_END_NEW, "the acceptance pass ends before the fetch"),
        (IRQFETCH, IRQFETCH_NEW, "the acknowledge byte from cpu_bus.irq_ack"))]


def source_dir(arg):
    """A folder given to this script: it must hold z80.cpp and must NOT hold z80.inc.  The script would not open that
    file anyway (read() takes the pinned names only), but a folder with it in is a MAME checkout or a copy of one, and
    the rule is kept where it cannot be missed: the inputs are folders of the pinned files and nothing else."""
    if not os.path.isfile(os.path.join(arg, "z80.cpp")):
        sys.exit("no z80.cpp in %s" % arg)
    for name in os.listdir(arg):
        if name.lower() == NOT_TAKEN:
            sys.exit("REFUSED: %s holds %s, which has no licence header and is not taken (mame_z80/PINNED.txt).\n"
                     "Give this script a folder with the pinned files only, not a MAME checkout: %s for the first, "
                     "z80.cpp at %s for the second." % (arg, NOT_TAKEN, ", ".join(SHA256), DEFINES_REVISION[:7]))
    return arg


def read(d, name, sha256=None, revision=REVISION):
    if name not in SHA256 or name.lower() == NOT_TAKEN:
        sys.exit("REFUSED: %s is not one of the pinned inputs" % name)
    raw = open(os.path.join(d, name), "rb").read().replace(b"\r\n", b"\n")
    if hashlib.sha256(raw).hexdigest() != (sha256 or SHA256[name]):
        sys.exit("%s in %s is not the pinned revision %s (sha256 differs)" % (name, d, revision))
    return raw


def short_names(d):
    """The block of short names: lines 128-206 of z80.cpp at DEFINES_REVISION (under that file's BSD-3-Clause
    header), brought to the pinned z80.h's members by DEFINES_SUBS.  Every substitution must match exactly once, and
    the result must define exactly DEFINES_NAMES, each once, with no 2024 member spelling left."""
    lines = read(d, "z80.cpp", DEFINES_SHA256, DEFINES_REVISION).decode("utf-8").split("\n")
    assert lines[:2] == ["// license:BSD-3-Clause", "// copyright-holders:Juergen Buchmueller"], lines[:2]
    seg = lines[DEFINES_FIRST - 1:DEFINES_LAST]
    assert seg[0] == DEFINES_OPENER, "the 2024 z80.cpp's line %d moved: %r" % (DEFINES_FIRST, seg[0])
    assert seg[-1].startswith(DEFINES_CLOSER), "the 2024 z80.cpp's line %d moved: %r" % (DEFINES_LAST, seg[-1])
    text = "\n".join(seg) + "\n"
    for old, new, why in DEFINES_SUBS:
        if text.count(old) != 1:
            sys.exit("the short names: substitution found %d time(s), not once: %s" % (text.count(old), why))
        text = text.replace(old, new)
    defined = [ln.split()[1] for ln in text.split("\n") if ln.startswith("#define ")]
    missing = [n for n in DEFINES_NAMES if defined.count(n) != 1]
    extra = [n for n in defined if n not in DEFINES_NAMES]
    if missing or extra:
        sys.exit("the short names: not defined exactly once: %s; not expected: %s" % (missing or "none", extra or "none"))
    values = [ln.split("//")[0] for ln in text.split("\n") if ln.startswith("#define ")]
    old_spelling = [v.split()[1] for v in values if ".w.l" in v or v.rstrip().endswith(".d") or "m_shared_addr" in v
                    or v.split()[2:] in (["m_q"], ["m_qtemp"])]
    if old_spelling:
        sys.exit("the short names: a 2024 member spelling is left in %s" % old_spelling)
    return text.rstrip("\n").split("\n")


def generate_hxx(raw):
    """MAME's z80.hxx: what the pinned z80make.py writes for the pinned z80.lst.  `raw` is the files' bytes as read()
    passed them, and those bytes are what runs and what is read: the generator and the .lst are written to a scratch
    folder first, so nothing changed on disk since the check can get in.  -I: no environment, no user site, no script
    or current folder on the import path; an empty current folder.  The generator opens the .lst named by its first
    argument and writes the file named by its second; it prints nothing."""
    with tempfile.TemporaryDirectory() as tmp:
        src, empty = os.path.join(tmp, "pinned"), os.path.join(tmp, "empty")
        os.mkdir(src)
        os.mkdir(empty)
        for name in ("z80make.py", "z80.lst"):
            open(os.path.join(src, name), "wb").write(raw[name])
        hxx = os.path.join(tmp, "z80.hxx")
        p = subprocess.run([sys.executable, "-I", os.path.join(src, "z80make.py"), os.path.join(src, "z80.lst"), hxx],
                           cwd=empty, capture_output=True)
        if p.returncode != 0 or p.stderr or p.stdout:
            sys.exit("z80make.py failed (exit %d): %s" % (p.returncode,
                                                         (p.stderr or p.stdout).decode("utf-8", "replace")[-300:]))
        out = open(hxx, "rb").read().replace(b"\r\n", b"\n")   # a text-mode write: CRLF on Windows
    if hashlib.sha256(out).hexdigest() != HXX_SHA256:
        sys.exit("the generator's output is not the pinned one (sha256 %s, not %s)"
                 % (hashlib.sha256(out).hexdigest(), HXX_SHA256))
    return out.decode("utf-8")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    d, d_names = source_dir(sys.argv[1]), source_dir(sys.argv[2])   # (both refused if either holds z80.inc)
    raw = {name: read(d, name) for name in SHA256}      # every file checked before anything of them is used
    names = short_names(d_names)
    files = {name: raw[name].decode("utf-8").split("\n") for name in raw}
    hxx = generate_hxx(raw)
    cpp, lst = files["z80.cpp"], files["z80.lst"]
    assert cpp[0] == "// license:BSD-3-Clause" and cpp[1] == "// copyright-holders:Juergen Buchmueller", cpp[:2]
    assert files["z80.h"][:2] == cpp[:2], files["z80.h"][:2]
    assert lst[:2] == ["# license:BSD-3-Clause", "# copyright-holders:Juergen Buchmueller, Andrei I. Holub"], lst[:2]
    assert files["z80make.py"][1:3] == ["# license:BSD-3-Clause", "# copyright-holders:Andre I. Holub"]
    assert cpp[21].startswith("*****") and cpp[21].endswith("*/"), cpp[21]
    out = cpp[0:2] + [
        "//",
        "// z80_mame_machine.cpp -- GENERATED by extract_z80_machine.py from MAME's src/devices/cpu/z80/ at",
        "// %s (mame_z80/PINNED.txt): z80.cpp and z80.h from exact line ranges, and" % REVISION,
        "// execute_run's loop (MAME's z80.hxx) as the pinned z80make.py (BSD-3-Clause, copyright-holders: Andre I.",
        "// Holub) writes it for the pinned z80.lst (BSD-3-Clause, copyright-holders: Juergen Buchmueller, Andrei I.",
        "// Holub); and the short register and flag names from z80.cpp at %s" % DEFINES_REVISION,
        "// (BSD-3-Clause, copyright-holders: Juergen Buchmueller).  Framework calls replaced as marked 'CHANGED'.  Do",
        "// not edit: change the script and regenerate.  The shim it compiles against: z80_mame.hpp.  Our step driver",
        "// and the cpu.h API: z80_mame.cpp, which includes this file (build that one, never this one alone).",
        "//",
        "// Upstream's header comment (z80.cpp), kept:",
    ] + cpp[2:22] + [
        "",
        '#include "z80_mame.hpp"',
        "",
        "// ---- upstream z80.cpp at %s, lines %d-%d: the short names -------" % (DEFINES_REVISION[:11], DEFINES_FIRST,
                                                                                DEFINES_LAST),
        "// z80.lst and z80.cpp are written in these names.  At the pinned revision upstream keeps their definitions in",
        "// z80.inc, which was NOT taken because it has no licence header (at the pin and on upstream's master of",
        "// 2026-11-20).  This block is copied from the BSD-3-Clause z80.cpp at %s" % DEFINES_REVISION,
        "// (sha256 %s)," % DEFINES_SHA256,
        "// where upstream last published them under that header, with the named substitutions marked CHANGED: the",
        "// members as the pinned z80.h spells them, YXF added, and the names for members that are gone dropped.",
    ] + names
    n_ranges = 0
    for name, a, b, opener, what in RANGES:
        if name == "CLASS_HEAD":
            out.append("")
            out.extend(CLASS_HEAD)
            continue
        seg = files[name][a - 1:b]
        assert seg[0].startswith(opener), "%s line %d moved: %r" % (name, a, seg[0])
        out.append("")
        out.append("// ---- upstream %s lines %d-%d: %s " % (name, a, b, what) + "-" * max(0, 50 - len(what)))
        out.extend(BEFORE.get((name, a, b), []))
        out.extend(seg)
        out.extend(AFTER.get((name, a, b), []))
        n_ranges += 1
    text = "\n".join(out) + "\n"
    assert text.count(HXX_INCLUDE) == 1, "execute_run's #include moved"
    text = text.replace(
        HXX_INCLUDE,
        "// ---- CHANGED: in place of #include \"cpu/z80/z80.hxx\", that file: what z80make.py writes for z80.lst;\n"
        "// ---- %d lines, sha256 %s\n" % (hxx.count("\n"), HXX_SHA256)
        + hxx + "// ---- (z80.hxx ends) ----\n")
    for old, new, why, n in SUBS:
        assert text.count(old) == n, "substitution anchor found %d time(s), not %d: %s" % (text.count(old), n, why)
        text = text.replace(old, new)
    open(OUT, "w", encoding="utf-8", newline="\n").write(text)
    print("wrote %s (%d lines: the short names from the 2024 z80.cpp, %d upstream ranges and the generated z80.hxx)"
          % (os.path.basename(OUT), text.count("\n"), n_ranges))


if __name__ == "__main__":
    main()
