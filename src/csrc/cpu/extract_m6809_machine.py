"""Generate m6809_mame_machine.cpp from MAME's 6809 core (src/devices/cpu/m6809): the class's members, the inline
functions, reset, the input lines and the instructions, with the framework replaced by named substitutions (each listed
below and marked 'CHANGED' in the output).  It refuses to run if a source is not the pinned revision or an anchor line
has moved.

    python src/csrc/cpu/extract_m6809_machine.py <MAME checkout, or the folder holding m6809.cpp>

The pinned sources: MAME 1c924ea79551fdfc3960bfa8ceb93fc1928c661b, src/devices/cpu/m6809/ (sha256 of each file with LF
line ends below; mame_m6809/PINNED.txt).  BSD-3-Clause, copyright-holders: Nathan Woods (m6809make.py: Olivier
Galibert); the generated file keeps those notices.

MAME's 6809 is itself generated: m6809.lst (with base6x09.lst) describes every instruction cycle by cycle, and MAME's
build runs m6809make.py on it to make m6809.hxx, the state machine inside execute_one's switch.  So there is no
finished upstream file to take the instructions' line ranges from.  This script makes m6809.hxx as MAME's build does:
it runs the PINNED generator on the PINNED .lst (the bytes whose sha256 it has just checked, copied to a scratch
folder; python -I, from an empty folder), checks the sha256 of what the generator printed, and puts that text in
place of execute_one's #include.  The rest is exact line ranges, as for the other MAME cores.  Left out: the device framework (constructors and device
types, address spaces, the memory interface classes, the state table, save states, the disassembler, the clock
divider) and execute_run -- our step driver in m6809_mame.cpp follows it (CONTRACT.md 14).  The shim the output
compiles against is m6809_mame.hpp.
"""
import hashlib
import os
import subprocess
import sys
import tempfile

REVISION = "1c924ea79551fdfc3960bfa8ceb93fc1928c661b"
SHA256 = {   # of the file with LF line ends (as raw.githubusercontent.com serves it)
    "m6809.cpp": "ba2e2a018b033d4301cc00b219c698c89d0957267d1e977e0b93e1ccfd63f199",
    "m6809.h": "a8581492454d4123fc2548725ad9e7fec0430c0b93e072e4e92a3761a4b72d59",
    "m6809inl.h": "416affb0e6593dd6544576f8f298e897f69d5de0fc736583346da945c472bf60",
    "m6809.lst": "f1395c6e7741903b86c0c4e003a649999e66f86b03fdd38eb565f635c77162ba",
    "base6x09.lst": "601f44e2b2b87177d276f90072f8b433c10007ae508ae83deab10f76605fda89",
    "m6809make.py": "4c64da3f52075a7cbe292d8fbf51ca04f29e02fece04fc24b68e75dd0d66e547",
}
# what the pinned m6809make.py prints for the pinned m6809.lst (MAME's m6809.hxx), with LF line ends
HXX_SHA256 = "e63650e1719efd8ffb50192051153d4cc358ba245db8253febfea7997cab270d"
HXX_INCLUDE = '#include "cpu/m6809/m6809.hxx"\n'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "m6809_mame_machine.cpp")

# The class's head, ours: in place of m6809.h lines 27-83 (the framework's base, the memory interface classes and the
# overrides).  m6809.h's members follow it, from line 85.
CLASS_HEAD = [
    "// ---- ours: the class's head, in place of m6809.h lines 27-83 (the framework's base and overrides) -------------",
    "class m6809_base_device : public m6809_shim   // CHANGED: no cpu_device base: m6809_mame.hpp's shim",
    "{",
    "public:",
    "\texplicit m6809_base_device(const cpu_bus *bus) : m6809_shim(bus) { }",
    "",
    "\t// our step driver (m6809_mame.cpp)",
    "\tint drv_pass();                           // execute_one until the instruction ends, waits or is cut",
    "\tvoid drv_fetched(int page);               // an opcode byte was fetched: Table 9's undefined ones counted",
    "",
    "\t// m6809.h lines 59-83, the few kept (no longer virtual)",
    "\tvoid init_state();                        // device_start()'s first values (the generated file)",
    "\tvoid device_reset();",
    "\tvoid execute_set_input(int inputnum, int state);",
    "",
]

# (file, first line, last line, the text that must open the range, what it is); lines as sed and grep count them
RANGES = [
    ("m6809.cpp", 124, 130, "// turn off 'unreferenced label' errors", "the generated machine's labels"),
    ("CLASS_HEAD", 0, 0, "", ""),
    ("m6809.h", 85, 160, "\t// addressing modes", "the addressing modes, CC's bits, the vectors, the D register"),
    ("m6809.h", 165, 187, "\t// CPU registers", "the registers and the internal state"),
    ("m6809.h", 189, 288, "\t// Callbacks",
     "the callbacks, the bus accesses, the state stack, the operand and flag helpers"),
    ("m6809.h", 295, 298, "\t// other state", "the state stack's word, the branch condition, the free run"),
    ("m6809.h", 304, 304, "\tinline void execute_one();", "execute_one"),
    ("m6809.h", 344, 345, "#define M6809_IRQ_LINE", "the input lines' numbers"),
    ("m6809inl.h", 12, 278, "//-------------------------------------------------", "the inline functions"),
    ("m6809.cpp", 188, 212, "\t// initialize variables", "the power-on values (device_start's middle part)"),
    ("m6809.cpp", 250, 265, "void m6809_base_device::device_reset()", "reset"),
    ("m6809.cpp", 457, 479, "void m6809_base_device::execute_set_input(int inputnum, int state)", "the input lines"),
    ("m6809.cpp", 502, 567, "uint16_t m6809_base_device::read_tfr_exg_816_register(uint8_t reg)",
     "TFR's and EXG's registers"),
    ("m6809.cpp", 575, 578, "void m6809_base_device::log_illegal()", "log_illegal (never reached: see the driver)"),
    ("m6809.cpp", 585, 591, "void m6809_base_device::execute_one()", "execute_one: the generated state machine"),
]

# before and after these ranges, lines of our own
BEFORE = {
    ("m6809.cpp", 188, 212): [
        "void m6809_base_device::init_state()   // CHANGED (ours): device_start()'s first values only; upstream's",
        "{                                       // device_start sets the framework up before and after them",
    ],
}
AFTER = {
    ("m6809.h", 304, 304): ["};   // (ours) the class ends: m6809.h line 306"],
    ("m6809.cpp", 188, 212): ["}   // CHANGED (ours): closes init_state()"],
}

SYNC_LOOP = ("\twhile(!m_nmi_asserted && !m_firq_line && !m_irq_line)\n\t{\n\t\teat_remaining();\n"
             "\t\tif (UNEXPECTED(m_icount <= 0)) { push_state(118); return; }\nstate_118:\n\t\t;\n\t}\n")

# (upstream text, replacement, why, how many times it must match) -- applied to the copied text and to the generator's
SUBS = [
    ("\tvirtual bool hd6309_native_mode()               { return false; }\n",
     "\tbool hd6309_native_mode()                       { return false; }   // CHANGED: not virtual (the 6809 only)\n",
     "hd6309_native_mode", 1),
    # the waiting states (SYNC, CWAI): upstream burns the rest of its time slice and looks again at the next one
    ("\tdebugger_wait_hook();\n\teat(m_icount);\n",
     "\t// CHANGED: a wait is ONE E cycle and ends the step (m6809_mame.cpp, a slot: CONTRACT.md 14).  Upstream eats\n"
     "\t// the rest of its time slice here.  m_icount = 0 makes the generated check after this call suspend the\n"
     "\t// instruction; what the step had left is kept for the driver's count\n"
     "\tm_drv_waiting = true;\n"
     "\tm_drv_left = m_icount - 1;\n"
     "\tm_icount = 0;\n",
     "eat_remaining: a one-cycle slot", 1),
    # interrupts are sampled by the driver, at a step's A only
    ("\tswitch(get_pending_interrupt())\n",
     "\tswitch(m_drv_sample ? get_pending_interrupt() : 0)   // CHANGED: sampled only in a step's acceptance pass\n"
     "\t// (m6809_mame.cpp, phase A); the instruction's own pass (E) starts here without looking, so a line the\n"
     "\t// board raises at the boundary (D) is taken at the NEXT step's A (CONTRACT.md 1)\n",
     "MAIN samples at A only", 1),
    # SYNC: Motorola's data sheet (DS9845-R2, 1983), Figure 18 sheet 2 (p.23): after the opcode fetch and the
    # don't-care cycle at NNNN+1 comes a first sync-acknowledge (3-state) cycle, THEN the test "Interrupt Present?",
    # and one more 3-state cycle after it: 4 cycles at least (Table 9, p.29: SYNC ">=4").  Upstream tests first: 3.
    (SYNC_LOOP,
     "\tdo   // CHANGED: the first sync-acknowledge cycle runs before the test (Motorola's Figure 18 sheet 2; Table 9:\n"
     "\t     // SYNC is >= 4 cycles).  Upstream tests first, so a SYNC with a line already asserted took 3\n"
     + SYNC_LOOP[SYNC_LOOP.index("\t{\n"):]
     + "\twhile(!m_nmi_asserted && !m_firq_line && !m_irq_line);   // CHANGED: upstream's test, now after the cycle\n",
     "SYNC: the acknowledge cycle before the test", 1),
    # undefined opcodes: upstream runs them (its own choices) and never says so; counted by Table 9's map
    ("\tm_lic_func(ASSERT_LINE);\n\tm_opcode = read_opcode();\n",
     "\tm_lic_func(ASSERT_LINE);\n\tm_opcode = read_opcode();\n"
     "\tdrv_fetched(0);   // CHANGED (ours): an opcode Motorola's Table 9 leaves unused is counted (m6809_mame.cpp)\n",
     "page 1's fetch counted", 1),
    ("DISPATCH10:\n\tm_opcode = read_opcode();\n",
     "DISPATCH10:\n\tm_opcode = read_opcode();\n\tdrv_fetched(1);   // CHANGED (ours): as page 1's, by page 2's map\n",
     "page 2's fetch counted", 1),
    ("DISPATCH11:\n\tm_opcode = read_opcode();\n",
     "DISPATCH11:\n\tm_opcode = read_opcode();\n\tdrv_fetched(2);   // CHANGED (ours): as page 1's, by page 3's map\n",
     "page 3's fetch counted", 1),
]


def source_dir(arg):
    for d in (arg, os.path.join(arg, "src", "devices", "cpu", "m6809")):
        if os.path.isfile(os.path.join(d, "m6809.cpp")):
            return d
    sys.exit("no m6809.cpp in %s" % arg)


def read(d, name):
    raw = open(os.path.join(d, name), "rb").read().replace(b"\r\n", b"\n")
    if hashlib.sha256(raw).hexdigest() != SHA256[name]:
        sys.exit("%s is not the pinned revision %s (sha256 differs)" % (name, REVISION))
    return raw


def generate_hxx(raw):
    """MAME's m6809.hxx: what the pinned m6809make.py prints for the pinned m6809.lst.  `raw` is the files' bytes as
    read() passed them, and those bytes are what runs and what is read: the generator and the two .lst files are
    written to a scratch folder first, so nothing changed on disk since the check can get in.  -I: no environment, no
    user site, no script or current folder on the import path; an empty current folder; the .lst path with forward
    slashes, because the generator finds base6x09.lst beside it by splitting at the last '/'."""
    with tempfile.TemporaryDirectory() as tmp:
        src, empty = os.path.join(tmp, "pinned"), os.path.join(tmp, "empty")
        os.mkdir(src)
        os.mkdir(empty)
        for name in ("m6809make.py", "m6809.lst", "base6x09.lst"):
            open(os.path.join(src, name), "wb").write(raw[name])
        lst = os.path.join(src, "m6809.lst").replace(os.sep, "/")
        p = subprocess.run([sys.executable, "-I", os.path.join(src, "m6809make.py"), lst], cwd=empty,
                           capture_output=True)
    if p.returncode != 0 or p.stderr:
        sys.exit("m6809make.py failed (exit %d): %s" % (p.returncode, p.stderr.decode("utf-8", "replace")[-300:]))
    raw = p.stdout.replace(b"\r\n", b"\n")   # print() writes CRLF on Windows
    if hashlib.sha256(raw).hexdigest() != HXX_SHA256:
        sys.exit("the generator's output is not the pinned one (sha256 %s, not %s)"
                 % (hashlib.sha256(raw).hexdigest(), HXX_SHA256))
    return raw.decode("utf-8")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    d = source_dir(sys.argv[1])
    raw = {name: read(d, name) for name in SHA256}      # every file checked before anything of them is used
    files = {name: raw[name].decode("utf-8").split("\n") for name in raw}
    hxx = generate_hxx(raw)
    cpp = files["m6809.cpp"]
    assert cpp[0] == "// license:BSD-3-Clause" and cpp[1] == "// copyright-holders:Nathan Woods", cpp[:2]
    assert files["m6809make.py"][1:3] == ["# license:BSD-3-Clause", "# copyright-holders:Olivier Galibert"]
    assert cpp[107].startswith("*****") and cpp[107].endswith("*/"), cpp[107]
    out = cpp[0:2] + [
        "//",
        "// m6809_mame_machine.cpp -- GENERATED by extract_m6809_machine.py from MAME's src/devices/cpu/m6809/ at",
        "// %s (mame_m6809/PINNED.txt): m6809.cpp, m6809.h and m6809inl.h from" % REVISION,
        "// exact line ranges, and the instructions' state machine (MAME's m6809.hxx) as the pinned m6809make.py",
        "// (BSD-3-Clause, copyright-holders: Olivier Galibert) prints it for the pinned m6809.lst and base6x09.lst;",
        "// framework calls replaced as marked 'CHANGED'.  Do not edit: change the script and regenerate.  The shim it",
        "// compiles against: m6809_mame.hpp.  Our step driver and the cpu.h API: m6809_mame.cpp, which includes this",
        "// file (build that one, never this one alone).",
        "//",
        "// Upstream's header comment (m6809.cpp), kept:",
    ] + cpp[2:108] + [
        "",
        '#include "m6809_mame.hpp"',
    ]
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
    assert text.count(HXX_INCLUDE) == 1, "execute_one's #include moved"
    text = text.replace(
        HXX_INCLUDE,
        "// ---- CHANGED: in place of #include \"cpu/m6809/m6809.hxx\", that file: what m6809make.py prints for\n"
        "// ---- m6809.lst (which includes base6x09.lst); %d lines, sha256 %s\n" % (hxx.count("\n"), HXX_SHA256)
        + hxx + "// ---- (m6809.hxx ends) ----\n")
    for old, new, why, n in SUBS:
        assert text.count(old) == n, "substitution anchor found %d time(s), not %d: %s" % (text.count(old), n, why)
        text = text.replace(old, new)
    open(OUT, "w", encoding="utf-8", newline="\n").write(text)
    print("wrote %s (%d lines: %d upstream ranges and the generated m6809.hxx)"
          % (os.path.basename(OUT), text.count("\n"), n_ranges))


if __name__ == "__main__":
    main()
