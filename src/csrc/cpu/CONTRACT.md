# The CPU contract, version 2 (draft for review)

`cpu.h` declares the interface; this defines its behaviour. Every core in libssi263speech implements it: the Z180
(the Braille Lite), the 8085 (the Accent-PC), and later an x86 real-mode core. A board drives a core only through
`cpu.h`.

Version 2 follows Astra's review (Reply 78). The main change: **two execution paths**, kept apart.

- **The corrected path** (`*_step`, `*_run`): the semantics this contract defines, the target for every new core.
- **The legacy compatibility path** (`z180_run_legacy`): today's z180emu run slice, exceptions included, so that the
  current Braille Lite goldens hold while the board moves onto `cpu.h`. It is not a model of the chip. It is a
  fixed reference, retired once the Z180 comes from MAME (Tomi's decision: MAME's BSD-3 Z180 extracted behind this
  interface) and new goldens are accepted.

Sources are labelled throughout: **(chip)** the manufacturer's documented behaviour; **(model)** a modelling choice;
**(legacy)** observed in today's core, kept only for compatibility.

## 1. A step on the corrected path

`*_step()` performs exactly one step, in these phases, and returns its T-states:

| Phase | What happens | `*_cycles()` during it |
|---|---|---|
| A. Acceptance | At most one pending interrupt (priorities in 4), **by type**. *Vectored* (Z180 TRAP, NMI, IM1, IM2; 8085 TRAP and the RSTs): the return address is pushed, any vector byte is read (`irq_ack`, byte 0), the PC is set. *Injected* (8085 INTR, Z180 IM0): nothing is pushed and nothing is read here; the step's instruction at E comes from the acknowledge instead (4). Either way, a halted core leaves HALT. | the step's start (not yet charged) |
| B. Acceptance charge | The acceptance T-states are added, and the on-chip timers are clocked by them. | start + acceptance |
| C. EI shadow ends | (see 5) | start + acceptance |
| D. Boundary | `steps` increments; the on-chip serial port catches up to the current count and refreshes its interrupt level; then `bus->boundary(ctx, pc)`. | start + acceptance |
| E. Instruction | One instruction, or one HALT or SLP slot (6). Memory and I/O callbacks run here. | start + acceptance |
| F. Instruction charge | Its T-states are added; the on-chip timers are clocked by them. | start + acceptance + instruction |
| G. DMA | Z180: a cycle-stolen DMA transfer, charged and clocked. | as F, then + DMA |

Consequences worth stating:
- Anything the board raises in D (a key, the A/R request) is sampled at the **next** step's A **(legacy, kept as
  model)**.
- The serial port's interrupt level is refreshed at D, before the board's work. Moving that to F would make its
  request visible one boundary later **(legacy, kept as model)**.
- Burst-mode DMA (Z180) is its own step: a chunk of at most `Z180_DMA_CHUNK` bytes, with its own boundary at D, its
  T-states charged at F, and no instruction. The chunk size is fixed, so a step never depends on a budget.

## 2. Running a budget (corrected path)

`*_run(budget)` performs whole steps while the T-states run so far are below `budget`, and returns them: `>= budget`,
with an overrun under one step. `*_run(0)` returns 0 and has no side effects. A halted core keeps stepping in slots:
`*_run` never skips to the budget's end.

The Accent-PC board additionally uses `i8085_step_slice(c, remaining, &accepted_only)` for Python-host slice
compatibility. It runs one step except that a vectored acceptance reaching the remaining budget returns after C,
before D/E/F: no instruction, boundary callback or step increment has happened. The next call samples interrupts
normally before executing anything. It carries no previously executed instruction or its cost. Zero budget has no
CPU side effects and reports `accepted_only = 0`. This is an explicit board compatibility API; ordinary
`i8085_step` and `i8085_run` retain the whole-step rules above. The board's `python_split` and `split_effects` tests
check the boundary PC, clocks, step count and the absence of early I/O, memory and register effects.

## 3. The legacy compatibility path (`z180_run_legacy`)

`z180_run_legacy(budget)` reproduces today's `cpu_execute_z180(budget)` exactly, **its exceptions included**:

- **NMI is sampled at slice entry only**, not at each boundary (Astra, Reply 78: ten NOPs in one 30-cycle call
  finish with an NMI raised from the first hook still pending; one-cycle calls take it on the next call).
- **A burst DMA chunk takes the rest of the slice's budget**, with a single boundary callback.
- **SLP ends the slice**: the remaining budget is dropped while the timers are clocked for that one step only (608
  cycles reported against 8 clocked, Reply 76).

A sequence of `*_step()` calls, or of 1-cycle legacy calls, is **not** equivalent to one legacy call. Nothing may
claim otherwise. Each exception needs a synthetic test, because the Braille Lite goldens may never exercise them.
Which tests already exist and which are still to write is listed in 10.

## 4. Interrupts

Lines are set with `*_set_irq(line, asserted)` and sampled at A. `asserted` is the line's logical state, not its
pin level: an active-low pin asserted is electrically low (the 8086's TEST, below).

**Z180 (chip, except where marked):**
- TRAP, raised by an undefined opcode, comes first. It is not an acceptance at A: it is found at E, at the
  undefined byte's fetch, and is that step's instruction (its fetches, the read at IX+d for a DDCB/FDCB form, the
  stacking of PCH to SP-1 then PCL to SP-2, and the jump to 0000h), charged at F (printed pages 70-72, Figures
  32-33: 18 T for a 2nd op code, 26 T for a 3rd, plus programmed waits). Then NMI, on an edge.
- R counts op code fetches (M1 cycles, printed page 177), a trapped one and an IM0 acknowledge included.
- The on-chip requests are taken before the priority choice from their sources: PRT0/PRT1 as levels of TIF and
  TIE (a cleared TIF drops its request), the ASCI and CSIO likewise; the DMA completions are latched.
- IM0 timing: an injected instruction's opcode fetch is the 5-T acknowledge cycle (T1 T2 TW* TW* T3, two
  automatic waits: printed page 76, Figure 36), so an injected RST is 13 T without programmed waits; the
  acknowledge bytes take no programmed memory waits. An undefined injected form TRAPs, stacking the PC the
  interrupt found **(model)**.
- **(model)** Stack writes other than TRAP's (CALL, RST, the interrupt pushes) come from MAME's PUSH, which writes
  the low byte first: the stacked bytes are right, their bus order is not.
- Then the maskable sources, when IFF1 is set and no EI shadow is active: INT0, INT1, INT2 (levels, each gated by
  its ITC enable), then the on-chip sources in the Z180's order (PRT0, PRT1, DMA0, DMA1, CSIO, ASCI0, ASCI1).
- The ASCI request is a level that follows its status bits (Astra, Reply 15).
- **NMI acceptance** copies IFF1 into IFF2 and clears IFF1, so RETN restores the enable. **Maskable acceptance**
  clears both. There is no blanket "acceptance clears both" rule.

**8085 (chip, per Intel's documentation, checked against the pinned MAME source):**
- **TRAP** is non-maskable: neither IE nor the SIM masks affect it. It is edge and level sensitive: pending after a
  rising edge *while the line stays high*, so dropping the line before acceptance cancels it (unlike the RST 7.5
  latch). Acceptance saves IE for RIM.
- **RST 7.5**: an edge latch, cleared by acceptance or by SIM's R7.5 bit. It is subject to IE and its SIM mask.
- **RST 6.5, RST 5.5**: levels, subject to IE and their SIM masks.
- **INTR**: a level, subject to IE only (not the SIM masks). It is an **injected instruction**, and all of its
  acknowledge reads happen at E: the byte index *n* restarts at 0 for each acceptance, and `irq_ack(ctx, line, n)`
  supplies the opcode (*n* = 0) and then any operands, in place of memory fetches. It is the step's one instruction,
  executed exactly once, with no ordinary instruction after it in the same step. It does its own control transfer:
  an RST or CALL pushes and jumps as that instruction does, and an injected NOP pushes nothing. Its T-states are
  charged at F. The Z180's IM0 works the same way.
- **The Python core (`src/hosts/i8085.py`) is not an oracle.** It delays TRAP through the instruction after EI, and
  charges 12 T-states for an accepted interrupt where the pinned MAME source charges 11 (Reply 78). Each such
  difference is decided from Intel's documentation and recorded; neither implementation is copied blindly.
- **Decided from Intel's documentation** (2026-09-29, for review; MCS-80/85 Family User's Manual, Jan 1983):
  - An acceptance of TRAP or RST 5.5/6.5/7.5 is **12 T-states** **(chip)**: the RST listing (printed 5-14) gives
    "States: 12 (8085), 11 (8080)", and the hardware RESTART is that instruction generated internally ("it executes an OF
    machine cycle without issuing RD, generating the RESTART opcode instead", section 2.3.5, Figure 2-19: M1 of six
    states, then two memory writes). MAME's 11 is the 8080's; the extraction changes it (`I8085_ACCEPT_T`).
  - TRAP is **not** delayed by EI (5) **(chip)**: TRAP "is not subject to any mask or interrupt enable/disable
    instruction" (section 2.2.7); the EI listing's delay concerns "the interrupt system". The Python core's NOP
    before a queued TRAP is its own behaviour.
  - An injected instruction's T-states are its own: "The INA cycle is identical to an OF cycle" except INTA for
    RD, and a CALL's two further INA cycles are three states each (section 2.3.4, Figures 2-17/2-18): an injected
    RST is 12, a CALL 18, a NOP 4 **(chip)**. The PC is not incremented during INA cycles, so a conditional branch
    not taken leaves it where the interrupt found it (MAME moved it by 2; changed).
  - A conditional jump or call **not taken still reads its second byte** **(chip)**: the 8085 table (printed
    5-19) gives Jcond F R / F R R and Ccond S R / S R R W W, the sequence regardless of the condition being
    footnoted as the 8080A's. So an untaken one from the acknowledge asks for bytes 0 and 1 (7 or 9 T), a taken
    one 0, 1 and 2 (10 or 18 T), and an ordinary untaken one reads its opcode and byte 2, not byte 3. MAME read
    neither; changed (after Astra, Reply 98: `intr_jcc`, `intr_ccc` and their taken forms, `jcc_reads`,
    `ccc_reads`).
  - Intel excludes EI and DI as instructions supplied through INTR (printed 2-14 and 5-17): what the core does with
    those bytes is the emulator's, not manufacturer-defined INTR behaviour.
  - PSW bits 1, 3 and 5 are "X: Undefined" (the PUSH PSW listing): the cores may differ there, and MAME keeps its
    undocumented V and K flags in them.

**8086 (MAME's i8086, the pinned revision; its clock counts decided against Intel's manual where marked, the rest
documented as it behaves):**
- **Reset**: CS:IP = FFFF:0000 (linear FFFF0h), DS = ES = SS = 0, FLAGS F002h (IF and TF off; the 8086's bits 12-15
  read 1); a pending NMI edge is dropped, a held INTR is sampled again.
- **Order at A**: a pending NMI, else INTR when IF is set -- neither while the interrupt shadow is active (5). Both
  are *vectored*: FLAGS is pushed, IF and TF cleared, the vector read (INTR: `irq_ack(ctx, I86_INTR, 0)`, one byte,
  the 8259's second INTA; NMI: 2), then CS and IP pushed -- the IP where execution resumes -- and CS:IP loaded from
  the table. INTR is a level (dropped before acceptance, it is not taken); NMI an edge. A halted core leaves HALT.
  A halted core stops A there; otherwise the **single-step trap** comes next: armed by POPF or IRET with TF set, it
  is taken (INT 1) at the second boundary after, i.e. after one more instruction, never while the shadow is active
  (so MOV SS delays it by one instruction). An INTR and the trap can both be taken at one A (MAME's order).
- **Clock counts, decided from Intel's manual** (after Astra, Reply 104: *The 8086 Family User's Manual*, Oct 1979,
  9800722-03, Table 2-21, cited as printed page / PDF page; each is a named change of the extraction or of the
  driver, with its own must-fail control) **(chip)** except where marked:
  - **One CPU: the virtual 8086** (Astra, Reply 106). Every row is the 8086's; no 8088 row is mixed in (a future 8088
    needs its own bus, queue and timing model, not selected rows of this table). The host's coupling is unchanged:
    5 million instructions a second is a compatibility policy (10), and these counts move no write.
  - **Interrupt entry, by kind.** An INTR acceptance 61 T (7 transfers, the two INTA cycles included; 2-56/PDF 79),
    NMI 50 (2-60/PDF 83), the single-step trap 50, charged at B. **The trap's figure is a model choice, the sources
    disagree:** 50 in Table 2-21 (2-66/PDF 89) and in the 1985 *iAPX 86/88, 186/188 User's Manual* (210912-001),
    Table 1-42 (printed 1-121/PDF 137); 51 in the 1979 manual's AP-67 (printed A-28/PDF 332). Nothing here measured
    it; the disagreement is recorded, not resolved. INT n 51, INT 3 52, INTO 53 taken and 4 not (2-56/PDF 79), charged
    with the instruction at F. IRET 24, its FLAGS restore included (2-56/PDF 79). The divide error (DIV, IDIV, AAM
    0): the instruction's own charge (MAME's: DIV r8 80, AAM 0 none) plus an entry of 51 **(model)**: the manual
    gives no figure; like INT n it runs no INTA cycles (printed 2-25/PDF 48), so INT n's 51. MAME charged 0 for
    every entry, 2 for INT 3 and INTO, and 44 for IRET (32 + its POPF 12).
  - **An intercepted interrupt costs the same** as one taken through the table: the instruction's own entry (INT n
    51, INT 3 52, INTO 53, a divide error its instruction + 51). What the host does instead of the handler -- the
    handler's instructions and its IRET, the DOS or BIOS service time -- is **not modelled**: an intercept adds no
    time for it. A host that needs the service's duration must charge it itself.
  - **REP string forms**: 9 + n per repetition, n = MOVS 17 (2-61/PDF 84), CMPS 22 (2-53/PDF 76), SCAS 15
    (2-65/PDF 88), LODS 13 (2-60/PDF 83), STOS 10 (2-66/PDF 89); the REP prefix's own row (2, 2-63/PDF 86) is read
    as included in the 9, not added. A segment override is its row's 2 (2-65/PDF 88). With CX = 0: 9. A REP before a
    non-string instruction is a 2-T step of its own, the instruction the next step (MAME's; it charged 0). MAME
    charged 2 (the REP as an override) + the plain instruction per pass; its REP rows were unused and three of their
    counts differ (CMPS 21, SCAS 14, LODS 11).
  - **Prefixes**: a segment override 2, LOCK 2 (2-60/PDF 83; MAME charged NOP's entry), each paid once per
    instruction (see the REP clause below for passes).
  - **Odd addresses**: 4 T more for every word transfer at an odd address, memory or port ("For the 8086, add four
    clocks for each 16-bit word transfer with an odd address", every page of the table): an INT's three pushes at
    an odd SP add 12, IRET's pops likewise. MAME modelled none. Instruction fetches are not transfers (the queue).
  - NOP 3 (2-62/PDF 85; MAME 2); ESC 2 with a register, 8 + EA with memory (2-54/PDF 77; MAME 2 + EA).
  - **The stack, word-port, return, LDS/LES and word MUL/DIV rows: the 8086's** (Reply 106). MAME's had the 8088's
    4 per word transfer built in (so at an odd address the 4 was counted twice) and its returns matched neither CPU.
    Now: PUSH r16 11, PUSH mem 16 + EA, PUSH sreg 10, PUSHF 10 (2-63/PDF 86; MAME 15, 24, 14, 14); POP r16 8, POP
    mem 17 + EA, POP sreg 8 (2-62/PDF 85), POPF 8 (2-63/PDF 86; MAME 12, 25, 12, 12); IN AX 10 (imm8) and 8 (DX), as
    the byte forms (2-55/PDF 78; MAME 14, 12); OUT likewise (2-62/PDF 85); RET 8, RET n 12, RETF 18, RETF n 17
    (near/far, without/with the stack adjustment, 2-64/PDF 87; MAME 20, 24, 32, 31); LDS/LES 16 + EA (2-59/PDF 82;
    MAME 24); MUL, IMUL, DIV, IDIV m16 124, 134, 150, 171 + EA (2-61/PDF 84, 2-55/PDF 78, 2-54/PDF 77, 2-55/PDF 78;
    MAME 128, 138, 154, 175). The odd-address 4 is then added once per actual word transfer: a PUSH/POP at an odd SP
    once, RETF/RETF n and CALL far twice, PUSH/POP of an odd memory operand at an odd SP twice, LDS/LES at an odd
    address twice, a word IN/OUT at an odd port once, a byte never. Each is a named change of the extraction.
  - **WAIT: 3 + 5n** (2-67/PDF 90; Reply 106). Entering the instruction costs 3 and tests TEST: active, the WAIT is
    done in 3 (n = 0). Inactive, IP stays on the WAIT and each following step is one recheck, 5 T ("retests the
    TEST line at five-clock intervals", 2-18/PDF 41), the recheck that finds TEST active ending it: 3 + 5n in n + 1
    steps. An interrupt is accepted between rechecks ("after any ... wait test cycle", 2-24/PDF 47), pushing the
    WAIT's address; after its handler the WAIT is entered again, 3 **(model**: "the WAIT instruction is again fetched
    prior to servicing the interrupt", the 1985 manual's 1.7.8, printed 1-122/PDF 138**)**. A recheck is a step that
    follows, with nothing between, the step that left the same WAIT waiting (as a REP continuation). MAME charged 3
    on entry and ended its slice while waiting.
  - **TEST, logical and electrical.** `i86_set_irq(cpu, I86_TEST, asserted)` takes the LOGICAL state: `asserted`
    = 1 means TEST active, which on the pin (TEST-bar, active low) is the electrical LOW level ("If the TEST input is
    LOW execution continues, otherwise the processor waits", the 8086 data sheet, printed B-9/PDF 552); 0 means
    inactive, the pin HIGH, and a WAIT waits. A new core's TEST is asserted; `i86_reset` leaves it as it is (an
    input line, like INTR).
  - **Three more rows** (Reply 110), each changed in its own instruction's handler, so the rows upstream shared
    with it stay: **LOOPNE taken 19** ("19 or 5", 2-60/PDF 83; MAME LOOP's 17; not taken 5, as LOOP's); **TEST
    r8/r16,imm 5** and **TEST m8/m16,imm 11 + EA** (2-67/PDF 90; MAME the ALU's 4 and CMP's 10 + EA). TEST AL/AX,imm
    (A8/A9) keeps its own 4 (the same page), as do the 80h-83h group's reg,imm 4 and CMP mem,imm 10 + EA. A word
    TEST m16,imm at an odd address adds the 4 **(model**: the table's Transfers column shows "-" for this row, where
    every other memory,immediate row shows its transfers (CMP's and MOV's 1, 2-53/PDF 76 and 2-61/PDF 84; ADD's and
    the other ALU rows' 2) and TEST register,memory shows 1; the operand is read, so it is counted as one
    transfer**)**.
    LOOP 17/5 and LOOPE 18/6 were already Intel's.
  - MUL/DIV are one figure each, the lowest of Intel's data-dependent ranges. Which physical PC drove the card
    (8086 or 8088) is not established and need not be: the core is a named virtual 8086.
- **Forward progress**: every step costs at least 2 T-states -- every path charges an entry of Intel's table, the
  smallest being 2 -- an intercepted interrupt and a HALT or WAIT slot included, so `i86_run(budget)` always
  returns. Before these counts an INT n (0 T), AAM 0 (0 T) and a REP before a non-string instruction (0 T) could
  each make a 0-T step, and an INT whose vector pointed at itself kept `i86_run(cpu, 1)` looping for ever (Astra's
  probe, Reply 104). Tested: `forward_progress` (her probe and its relatives, guarded so a regression fails rather
  than hangs) and `step_cost_floor` (every first byte, alone and after REP or REPNE, with and without a seam that
  services every interrupt).
- **INT n / INT 3 / INTO / divide error and the host (`cpu_bus.intercept`)**: raised by the instruction, at E, and
  offered to `intercept(ctx, vector, kind)` first -- kind `I86_INT_SOFTWARE`, or `I86_INT_EXCEPTION` for a divide
  error -- with CS:IP already past the instruction (the 8086 pushes that address for a divide error too). Nonzero:
  the host serviced it; nothing is pushed and the instruction ends there, with the registers the host set through
  `i86_regs_set`. Zero or no callback: the core pushes and vectors. NMI, INTR and the trap are never offered (they
  happen at A). This is the seam a DOS/BIOS/EMS stand-in needs (the Accent-mini host), the equivalent of Unicorn's
  interrupt hook.
- **Defined flags: IMUL's CF and OF** (Reply 110) **(chip)**: set exactly when the upper half of the product (AH for
  a byte source, DX for a word) is not the sign extension of the lower half, i.e. the signed product does not fit
  the source's width ("If the upper half of the result ... is not the sign extension of the lower half of the
  result, CF and OF are set; otherwise they are cleared", printed 2-37/PDF 60). MAME tested AH != 0 / DX != 0,
  MUL's rule: it set them for every small negative product (-1 x 1) and cleared them on a positive overflow with a
  zero upper half (127 x 2, -128 x -1). The product itself was already right. A named change of the extraction for
  each width; AF, PF, SF and ZF, undefined after IMUL, are left as MAME computes them and not asserted. MUL is
  unchanged (CF = OF = the upper half nonzero, the same page).
- **Prefixes**: a segment override and the instruction it modifies are ONE step (MAME runs the prefix as its own
  loop turn but never dispatches an interrupt between them); the saved PC is the first prefix's. LOCK is MAME's own
  step and opens a one-instruction shadow.
- **A prefixed WAIT: a documented limitation, not a hardware claim** (Reply 110). A WAIT that waits puts IP back on
  its own 9Bh byte (MAME's `m_ip--`), after any prefix: its rechecks run without the prefix, an interrupt between
  rechecks pushes the WAIT's address rather than the prefix's, and after the handler the WAIT is entered again
  without it (3, not the prefix's 2 + 3). What the real 8086 keeps of a prefix across a WAIT's rechecks and an
  interrupt is not established, so this is left as it is and pinned by an executable reproducer (`wait_prefixed`,
  with a control showing it sees a restart at the prefix). The REP clause's prefix preservation below applies to
  REP string instructions only; it is not extended to WAIT or to any other instruction. The Accent-mini's driver
  has no prefixed WAIT.
- **REP string instructions**: one iteration per step. An interrupt is sampled between iterations; the pushed IP is
  the first prefix's, so the instruction resumes with all its prefixes (MAME; the real 8086 keeps only one). Each
  pass re-fetches its prefixes; the first pass pays them and the 9, a pass continuing the instruction the previous
  step left only its repetition (CS: REP MOVSB: 2 + 9 + 17 = 28 T, then 17 a pass). A pass after an interrupt's
  handler is a first pass again (28): the 8086 re-decodes the interrupted instruction **(model)**.
- **HLT** charges 2 T (CHANGED: MAME ends its slice instead); the slots are in 6.

## 5. EI

After EI, the next step's acceptance skips the **maskable** interrupts, so the instruction after EI runs first
**(chip)**. TRAP and NMI are not delayed.

8086: the shadow (`m_no_interrupt`) follows STI, POP SS, MOV sreg (any segment register, MAME) and LOCK; it holds
off NMI as well as INTR and the single-step trap for the one instruction after, and counts down at C.

## 6. HALT and SLP

A halted core does one **slot** per step, separate from the HLT instruction's own execution cost:
- Z180: 3 T-states **(legacy, kept as model)**.
- 8085: 4 T-states **(model, proposed)**.
- 8086: 2 T-states **(model)**; the HLT has completed, so `i86_pc()` and the address an acceptance pushes are both
  the byte after it. A WAIT that waits (TEST not asserted: the pin HIGH) is not a HALT slot: its entry is 3 T and
  each later step a 5-T recheck with IP held, Intel's 3 + 5n (4).
- Z80: 4 T-states **(chip:** the HALT state repeats an M1 cycle with a NOP forced: Zilog's HALT Exit; 15**)**.

Interrupt arrival is tested at both slot edges.

**SLP** (Z180) is **not** simply HALT with the peripherals running. Per Zilog's manual (Z8018x user manual, printed
pages 33–35) **(chip)**:
- **Wake without service.** An interrupt request that is *individually* enabled ends SLEEP even when IEF1 = 0. The
  CPU then continues at the instruction after SLP without vectoring. With IEF1 = 1 it is accepted as usual. HALT
  differs: a request masked by IEF1 = 0 leaves the CPU halted.
- **What stops.** Normal SLEEP stops DMA and refresh; the IOSTOP bit (ICR) changes which on-chip peripherals keep
  running.

On the corrected path, sleep is polled in the same fixed slots as HALT **(model)**, and each slot's T-states clock
the peripherals that the manual leaves running in that state. Required tests: wake-without-service, and IOSTOP
(10). The narrow clock-accounting correction measured so far (Reply 76) is part of this, not all of it. On the
legacy path, SLP keeps today's slice-ending quirk (3).

## 7. What callbacks see, by phase

- **A** (vectored acceptance only): the stack writes and any vector read (`irq_ack`, byte 0). `*_cycles()` is the
  step's start. `*_pc()` is the interrupted instruction's address: where execution resumes, the address acceptance
  pushes. For a halted core that is the address after its HALT or SLP **(model)**; during the slot itself (D, E)
  `*_pc()` stays on the HALT, or on SLP's first byte (MAME's convention; the legacy core reports SLP's second). There has been no boundary for the vector's first
  instruction yet. An injected instruction makes no bus access at A.
- **E** for an injected instruction: its bytes come from `irq_ack` (*n* = 0, 1, …), and any stack writes are the
  instruction's own.
- **D**: `serial_tx` (the serial catch-up), then `boundary`. `*_cycles()` is start + acceptance.
- **E**: memory, fetch and I/O. `*_cycles()` is start + acceptance. The instruction's own T-states come at F.
- **F**: timer-driven serial or interrupt effects happen inside the core and reach the board at the next D.
- **G**: DMA memory accesses. `*_cycles()` is start + acceptance + instruction.

Two PCs are distinct. `*_pc()` is the **saved instruction-start PC** of the current (or next) instruction.
`*_regs_get()` reports the **architectural** registers, including the PC as execution has moved it.

Getters have no side effects. In particular they never perform a RIM, and never consume the 8085's saved
post-TRAP IE.

Memory addresses are physical (after the Z180's MMU). Only external I/O reaches `in`/`out`. Cores are not
bus-cycle accurate: an access inside an instruction is not placed at its own T-state, and the Z180's wait states
(DCNTL) are applied as the chosen core applies them, documented per core.

## 8. The 8085's I/O timestamps

Start-stamped I/O (E: start + acceptance) is the one documented convention for every core **(model)**. Today's
Python core charges before executing, so its timestamps sit later by an amount that **varies per instruction** (its
opcode fetch already happens before the charge), not by a constant. Before migrating:
- capture today's Accent-PC ordered writes and timestamps as a reference;
- compare architectural state and events at explicitly normalised phases;
- check the host's speech and interrupt timing separately.

## 9. API obligations

- **Ownership.** `*_create` copies the `cpu_bus`. `ctx` must outlive the core.
  - Required callbacks: `read`, `write`, `in`, `out`.
  - Optional (NULL allowed): `fetch` (defaults to `read`), `irq_ack` (defaults to FFh), `serial_*`, `boundary`.
- **Reset** restores the architectural state and the chip's reset values, clears pending edges and latches,
  re-samples level lines on the next A, and zeroes `cycles` and `steps`.
- **`steps()`** increments at D, before `boundary` runs. So the board's scheduled work sees the new count, as
  today's board increments its counter before its key schedule.
- **Serial divisors** come from the chip's own registers. 16 is only the Braille Lite's configuration.
- **Instances.** No global mutable state. Distinct instances may run on distinct threads, and two instances
  interleaved in one thread must behave as each alone (a required test; `test_bl_board.c` does it today for the
  board).
- **Re-entry.** A callback may call `*_cycles`, `*_steps`, `*_pc`, `*_regs_get`, `z180_asci_get`, `z180_csio_*` and
  `*_set_irq`, but not `*_step`, `*_run`, `*_reset` or `*_destroy`.
- **Z180 only:** the CSI/O with an external clock (CNTR SS = 111): `z180_csio_clock` is the far end's 8 clocks. An
  armed transfer moves TRDR out (TE) and the given byte in (RE), clears TE and RE and sets EF; reading or writing TRDR
  clears EF; the request is EF and EIE, a level (section 4). Nothing happens when nothing is armed. The Z180's own
  clock (SS < 111) is not modelled. On z180emu the CNTR read still masks TE (upstream's), so a firmware waiting for TE
  to clear never waits there; the Blazie clock controller (`../blazie/bl_clock.h`) takes the byte at the next step
  boundary anyway. `z180_csio_cntr` reads CNTR without side effects.
- **Z180 only:** `z180_asci_get` reads an ASCI channel's control registers without side effects, so a board that
  carries the serial line to a real port can set that port to the format the firmware programmed (the Blazie
  emulator's COM port, `../blazie/bl_serial.h`). The pin bits are each core's own: z180emu's CNTLA0 readback sets
  MOD0 (its /RTS0 readback lands in bit 0) and clears RTS0, so a firmware read-modify-write stores 2 stop bits and
  RTS0 asserted; MAME's `cntlb_r` masks bit 5. The 8086's `intercept` may also call `i86_regs_set` (its whole purpose).
- **x86 only** (a host standing in for DOS writes the machine's registers): `i86_regs_set` between steps or from
  `intercept` (FLAGS bits 12-15 are forced to 1; setting TF does not arm the trap); `i86_next_pc` (CS * 16 + IP now,
  for a run-until test); `i86_aliased` (opcodes whose meaning differs on the 80186 and later: a program needing a
  later CPU shows there).
- **Memory** (8086): 20-bit linear addresses; a word is two byte accesses, low first, at consecutive linear addresses
  (so a word at offset FFFFh does not wrap inside its segment: MAME's); a word port access is two byte accesses, the
  port then port + 1 (the 8088's bus).
- **`*_destroy`** frees everything the core allocated.

## 10. How a core is accepted

**Tests that already exist** (Astra's, on copies of today's core):
- `investigation/section93-review/`: the SLP clock accounting (608 reported against 8 clocked; 602 and 602 once
  fixed) and the timer start from RLDR.
- `investigation/section95-review/`: NMI sampled at slice entry against per boundary, and the Python 8085's EI/TRAP
  behaviour.

**Written for the MAME Z180 core** (`test_z180_contract.c`, in run_tests and the Linux gate; its must-fail
controls, each undoing one driver rule, are `contract_controls.py`):
- a zero budget; reset (counts zeroed, a pending NMI edge dropped, a held INT0 re-sampled); two instances
  interleaved;
- NMI taken at the step after the boundary that raised it (IFF1 into IFF2); the EI shadow;
- an interrupt raised at a HALT slot's boundary, accepted at the next step (the slot is 3 T);
- SLP wake-without-service with IEF1 = 0, and HALT staying halted in the same program;
- IOSTOP stopping the PRT;
- burst DMA in chunks of 16 bytes, each its own step with a boundary and no instruction;
- (after Astra, Reply 92) IM0 injected instructions: a NOP (nothing pushed, no instruction after it in the step),
  an RST and a CALL (their own pushes of the interrupted PC; the CALL's operands from acknowledge bytes 1 and 2),
  the byte index from 0; DMA stopped by SLEEP, including in the SLP's own step, with HALT's DMA as the positive
  control; an NMI waking a HALT and disabling DMA; the acceptance-phase `*_pc()`, from HALT too; the ASCI's
  error flags cleared by EFR = 0 and not by EFR = 1.

**Tests still to write:**
- refresh stopped in sleep (the core doesn't model refresh), and IOSTOP stopping the ASCI;
- the bus order of the other stack writes (see 4), if a board ever depends on it.

**After Astra, Replies 93/94:** `im0_rst_nowait` (13 T, R + 1), `im0_nop_nowait` (5 T), `im0_prefixed` (LD IX,nn
from the acknowledge: the PC held, R + 2), `im0_undefined` (DD 00h from the acknowledge TRAPs, stacking the
interrupted PC); the first three IM0 tests now assert their T-states (5, 19, 24 with reset's wait states);
`trapbus_2nd` (DD 24h: 18 T, R + 2, PCH written first), `trapbus_3rd` (DD CB 05 00: 26 T, R + 3, one read of
IX+5, PCH first), `trapbus_legal_ddcb` (the control: RLC (IX+5) reads IX+5); `prt_priority` (a waiting PRT0
overflow beats a waiting DMA0 completion in all 20 timer-clock phases; Astra's fixture had 3 of 20) and
`prt_stale` (with TIF0 cleared first, DMA0 is taken). `contract_controls.py` checks each program's exit code,
summary line and the full test inventory (a crash, a timeout or a missing test fails the run), and has 27 controls
(29 after Reply 95).

**After Astra, Reply 95:** an injected prefixed instruction keeps the PC it set only if it transfers control (JP
(IX)/(IY), RETN, RETI), decided from the acknowledge bytes, not from the final PC: `im0_jpix_collision`/`_ordinary`,
`im0_retn_collision`/`_ordinary`, `im0_wrap`; an injected LDIR runs one iteration with the PC put back
(`im0_ldir_once`, a model choice).

**Written for the MAME 8085 core** (`test_i8085_contract.c`, in run_tests and the Linux gate; its must-fail
controls, each undoing one rule of the driver, a named change of the extraction or an upstream rule, are
`i8085_controls.py`): `zero_budget`, `reset` (counts zeroed, a pending TRAP edge and the 7.5 latch dropped, the
masks set, a held RST 5.5 sampled again), `two_cores`; `trap_ei` (TRAP at EI's boundary taken before the next
instruction, 12 T), `ei_shadow`; `halt_edge` (the slot is 4 T, the HLT 5), `accept_pc`, `halt_masked`;
`trap_cancel` (edge AND level), `trap_edge`, `trap_priority`, `rim_after_trap` (and `i8085_regs_get` does not
consume it); `priority` (7.5, 6.5, 5.5, INTR); `rst75_latch` (latched while masked), `sim_reveal` (taken at the
next step's A, not inside SIM's step), `sim_r75`, `rst_levels`; `intr_rst`, `intr_call` (acknowledge bytes 0, 1,
2; 18 T), `intr_nop` (nothing pushed, 4 T), `intr_jcc`, `intr_twice` (the byte index restarts), `intr_halt`;
`sid_sod`.

**Written for the MAME 8086 core** (`test_i86_contract.c`, in run_tests and the Linux gate; its 75 must-fail
controls, each undoing one rule of the driver, a named change of the extraction or an upstream rule, are
`i86_controls.py`): `zero_budget`, `reset` (FFFF:0000, F002h, a pending NMI dropped, a held INTR sampled again),
`two_cores`; `intr_vector` (the three pushes, IF/TF cleared, one `irq_ack` byte 0, a 61-T acceptance, `i86_pc()`
at the pushes), `intr_masked`, `intr_level`, `sti_shadow`, `ss_shadow` (MOV SS and POP SS), `nmi_edge`;
`int_iret` (51 T and 24 T, Intel's), `intercept` (IP past the INT, nothing pushed, the host's AX kept; a declined
vector vectored), `intercept_kinds` (INTO, INT 3, the divide error with IP past the DIV), `hw_not_offered`; `halt`
(2 T, 2-T slots, the pushed address after the HLT), `halt_masked`, `wait` (3 when TEST is asserted; 3, 5, 5, 5
waiting: 3 + 5n), `wait_interrupt` (an INTR between rechecks pushes the WAIT's address; entered again after IRET,
3), `wait_prefixed` (Reply 110: a reproducer of today's prefixed WAIT, rechecked and re-entered at its 9Bh byte
without the prefix -- documented, not claimed as the 8086's); `prefix_atomic` (15 T, as Intel),
`rep_iteration` (28, 17, the resumed pass 28); `trap`, `trap_ss`; `aliased`; `tstates` (MOV r16,imm 4, OUT/IN DX
8, CLI/STI 2, JMP short 15: MAME = Intel; NOP 3, LOCK 2, ESC 2 and 8 + EA: Intel's); `io` (a word port access is
two bytes); `flags`; after Astra's Reply 104: `int_costs` (51, 52, 4 and 53, the same when intercepted),
`accept_costs` (INTR 61, NMI 50, the trap 50), `divide_error` (DIV r8 80 + 51, AAM 0 51, the same when
intercepted), `rep_counts` (9 + n for each string form, CX = 0, a REP before NOP), `odd_word` (+4 per word
transfer: MOV, MOVSW, INT's pushes, IRET's pops, a word port), `forward_progress`, `step_cost_floor`; after
Reply 106, each at even AND odd addresses with both counts absolute (an odd-minus-even check alone passes a wrong
base): `stack_counts` (PUSH/POP r16, sreg, mem, PUSHF/POPF; SP odd, and SP and the operand odd), `port_counts` (IN/OUT
AL and AX, imm8 and DX, even and odd ports), `return_counts` (RET, RET n, RETF, RETF n, CALL near and far),
`word_memory_counts` (DIV, IDIV, MUL, IMUL m16, LES, LDS); after Reply 110: `test_imm_counts` (TEST r,imm 5 and
m,imm 11 + EA, byte and word, even and odd; TEST AL/AX,imm and CMP's rows unchanged), `loopne_counts` (taken 19;
not taken 5 by CX and by ZF; LOOP and LOOPE unchanged), `imul_flags_byte` and `imul_flags_word` (CF/OF, register
and memory, each width's control failing it alone), `mul_flags` (MUL unchanged). Against Unicorn on the
Accent-mini's driver: `compare_i86_accent.py` (README.md).

**The legacy path's exceptions** (`test_z180_legacy.c`, on z180emu): `legacy_nmi_entry` (an NMI raised at a slice's
first boundary stays pending through that 30-cycle call and is taken at the next call's entry; one-cycle calls take
it at the next call), `legacy_burst` (a 100-cycle call from the DMA start: one boundary, 17 bytes, 102 cycles),
`legacy_slp_slice` (one 5000-cycle call: SLP reports the budget with no PRT0 wake; the HALT control wakes four
times).  Writing them found that `z180_set_irq(Z180_NMI)` never reached z180emu's NMI (line 3 is an IRQ slot there);
fixed in the adapter, and the test fails with the mapping undone.

**TRAP** (`z180_trap.hpp`, from the manual's op code maps): undefined second bytes after DD/FD (DD 00h, INC IXH,
EX DE,HL), ED (the Z80's NEG duplicate, IN (C)) and CB (SLL) trap with UFO = 0 and the stacked PC at the
instruction's start + 1; undefined DDCB/FDCB fourth bytes trap with UFO = 1 and start + 2; IEF1 unaffected;
software clears ITC.TRAP and cannot set it; legal prefixed instructions (LD IX, an (IX+d) CB form, NEG, MLT,
SRL, JP (IY)) are the negative controls.

**MAME's e0deaf3898b, one test per hunk** (each fails with its hunk reverted in `contract_controls.py`):
`timer_start` (enabling starts from RLDR), `tmdr1h` (TMDR1H is the high byte), `dma_done_di` (a DMA0 completion
while IFF1 = 0 is kept, and taken only after EI and its shadow: the DMA hunk and the internal-IRQ gating hunk), and
`dma1_level` in `test_z180_whitebox.cpp` (/DREQ1's edge or level sense is DMS1; cpu.h has no DREQ lines).

Acceptance per core:
- **The z180emu adapter (legacy path)**: the Braille Lite goldens, English and Spanish, bit for bit, through
  `bl_board.c` rewritten onto `cpu.h`; plus the legacy-exception tests above (`test_z180_legacy.c`).
- **MAME's Z180 extracted (corrected path)**:
  - the Z80 instruction exercisers, and the phase tests above;
  - then the Braille Lite firmware, compared with the legacy adapter. Every difference is explained before new
    goldens are accepted, including the serial protocol (the ordered host sends, cancels and completions), not
    just the spoken register values;
  - then a listen.
- **MAME's 8085 (corrected path)**: per-step traces against the Python core at normalised phases, each difference
  decided from Intel's documentation (4); the 8080/8085 exercisers; the interrupt, flag and timing tests; the
  captured Accent-PC reference (8).
- **MAME's 8086 (corrected path)**: the phase tests above; then the Accent-mini's driver against Unicorn, every chip
  write's value AND time identical, the audio, registers, host log and INIT snapshots identical, FLAGS by its
  stated policy (bits 0-11 equal at every software interrupt, OUT and the end; bits 12-15 masked but required to be
  the recorded 0h on Unicorn and Fh on MAME; Reply 106), memory after INIT identical but for the listed FLAGS images
  (`compare_i86_accent.py` fails the run on any of them); the timing questions of 4 decided from Intel's manual (the
  8088-flavoured rows replaced by the 8086's, Reply 106);
  then a listen. The host couples the CPU to chip time by **steps** (`cpu_ips` instructions per chip second), a
  compatibility policy kept from Unicorn, not a clock: the T-states are counted beside it (`i86_cycles`) and do not
  move a write.

The behavioural specification comes from the manufacturers' manuals. Observations of today's cores are labelled
**(legacy)** and are kept as regression facts, not as the definition of correct.

**Written for the MAME V40 core** (`test_v40_contract.c`; its must-fail controls, each undoing one rule of the
driver, the shim, a named change of the extraction or an upstream rule, are `v40_controls.py`): see 12.

**Written for the MAME 6809 core** (`test_m6809_contract.c`, in run_tests and the Linux gate; its must-fail controls
are `m6809_controls.py`, in run_tests): see 14.

**Written for the MAME Z80 core** (`test_z80_contract.c`, in run_tests and the Linux gate; its must-fail controls
are `z80_controls.py`, in run_tests; zexdoc and zexall by `test_z80_zex.c`, with their controls, where the exercisers
are at hand): see 15.

## 11. Licences

- `cpu.h` and this contract: MIT.
- The z180emu adapter links a GPL-2.0-or-later core, so any build containing it is GPL.
- MAME's Z180, 8085, NEC (V40), 8086, 6809 and Z80 files keep their BSD-3-Clause notices, each extracted dependency with its own actual notice
  and the upstream revision pinned.
- **Our dependency policy** for store builds (iOS): no GPL components, so neither z180emu nor Unicorn; MIT and BSD
  components with their notices kept; no firmware shipped (it is imported from Files). This is our policy. Apple's
  review guidelines don't require any particular licence, and choosing these licences doesn't by itself make an app
  acceptable.

## 12. The V40 (NEC uPD70208: the Speak-Out)

MAME's NEC core (`nec.cpp` and its parts, pinned in `mame_nec/PINNED.txt`) with the V40's parameters from `v5x.cpp`:
the V20's instruction set and clock counts, an 8-bit bus (a word is two byte accesses, low byte first), a 4-byte
prefetch queue at 4 clocks a byte, 20 address lines, no DIV quirk. The V40's on-chip ICU, TCU, SCU and DMA are **not**
in the core: a board models what its firmware uses and drives `V40_INT` from its interrupt controller.

- **Reset** **(MAME)**: PS = FFFFh, IP = 0, so the first fetch is at FFFF0h; the flags clear (IE off). Held lines
  are kept as 9 says (MAME's reset forgets a held INT; the driver asserts it again).
- **A step** is one instruction, or **one iteration of a REP string instruction** **(model)**, or a HALT slot of 2
  clocks **(model)**. A segment prefix and its instruction are one step **(MAME)**: an interrupt is never taken
  between them.
- **REP** **(chip: interruptible between iterations; the details are MAME's)**: an interrupt raised at an iteration's
  boundary is taken before the next iteration. It pushes the address of the instruction's FIRST prefix, with CW, IX
  and IY as far as they got, so IRET re-executes the whole prefixed instruction, a segment override included (before
  or after the REP), for the iterations left. Without an interrupt the override holds from step to step. The REP
  prefix's 2 clocks are charged once per instruction: MAME charges them at every re-entry of a split REP, which with
  one iteration per step would be every iteration (changed in the driver). Unicorn counts a REP of n iterations that
  ends by its count as n + 1 instructions (measured), this core as n steps.
- **INT** **(MAME)**: a level, sampled at A when IE is set and no shadow runs; its acceptance reads the vector
  NUMBER from `irq_ack(ctx, V40_INT, 0)` (the V40's ICU supplies 8-15 on the Speak-Out), pushes the flags, PS and IP,
  clears IE and BRK, and consumes the request: MAME clears the line, so a board asserts it again for a further one.
- **The interrupt response's clocks are open.** An acceptance costs 12 clocks here: MAME's `nec_interrupt` charges
  nothing of its own, only its PUSHF's (the V20 column). Twelve clocks of PUSHF are not evidence for the whole
  hardware response (for INT the acknowledge bus cycles, then the flags, PS and PC pushed and the vector read), and
  Intel's 8086 figures (51 for INT n, 61 for INTR) are Intel's software and hardware timings for another CPU: not
  substituted. No NEC figure is in hand: the V40 section of the 1990 data book, as read, gives none (nor do its V20
  and V30 sections: the INTAK pin and its bus timing, no response count); the instruction
  manual's clock table (Table 2-8, a V40 column) times instructions, its only interrupt note being CHKIND's; the data
  book's "27 + N clocks" interrupt latency is the V25's (uPD70320), a different CPU. The 12 stays, labelled MAME's,
  until a NEC source gives the response.
- **NMI** **(MAME)**: an edge, vector 2, taken whatever IE says, before INT.
- **Deferrals** **(chip: NEC's 1990 V-Series Data Book, the uPD70208 section, printed p.34 / PDF p.159; Instruction
  Manual U11301EJ5V0UMJ1)**: NMI and an enabled INT are taken at an instruction boundary, except:
  - after a **move to or from a segment register** (MOV sreg,r/m and MOV r/m,sreg; the manual's MOV caution, printed
    p.98 / PDF p.109, "dst = sreg or src = sreg"), **POP sreg** (any segment register: the manual's POP caution,
    printed p.118 / PDF p.129), **POLL**, and a **prefix**, both NMI and INT wait until the following instruction has
    run;
  - after **EI**, only the maskable interrupt waits ("EI instruction (maskable interrupts only)"; the manual's EI,
    printed p.80 / PDF p.91: "the interrupt is actually enabled when the single instruction following the EI
    instruction is executed"). NMI is not delayed.

  MAME had MOV to sreg, POP SS and BUSLOCK (`m_no_interrupt`, which holds NMI and INT). Changed in the extraction
  (after Astra, Reply 103): EI's delay, as its own counter that holds INT only (`drv_ei_shadow`; reusing
  `m_no_interrupt` would hold NMI too); a move FROM a segment register; POP DS0 and POP DS1; a completed POLL. A
  prefix and its instruction are one step already (above). Both counters run down at C. **Open:** a POLL that waits
  (its line inactive) is left as MAME has it, interruptible at each re-execution; no source says, and `cpu.h` has no
  POLL line, so it cannot occur here. Also not modelled: the data book's note that a block transfer's interrupt "may
  be delayed up to three bus cycles".
- **HALT** **(chip: NEC, data book printed p.34)**: RESET, NMI or INT releases it. NMI, and INT with IE set, are
  accepted before the instruction after the HLT, whose address is pushed. **An INT with IE = 0 releases HALT without
  an acknowledge**: execution resumes with the instruction after the HLT (no `irq_ack`), the request stays pending,
  and it is serviced once interrupts are enabled again (after EI and its one instruction). Release and acceptance are
  separate operations, both at a step's A, never mid-step at the line (changed: MAME releases at the line, so the
  instruction after the HLT ran before the interrupt): delivery stays at instruction boundaries. The release costs no
  clocks **(model: no figure)**. During the slots `v40_pc()` stays on the HLT; at A it is the address after it.
  Withdrawn: this clause's earlier rule that an INT with IE = 0 leaves the CPU halted "as on the 8086" -- NEC says
  otherwise for the V40.
- **Undefined opcodes** **(MAME)**: executed as MAME executes them (0x63, 0x66, 0x67, 0xF1: 10 clocks and nothing
  else; SETALC; the undefined shift and group forms) and counted in `v40_regs.undefined`, with the last one's address.
  The FPO escapes (D8h-DFh) are the coprocessor's, defined, not counted. **The 8080 emulation mode** (BRKEM) is not
  built: entering it sets `v40_regs.fault` and the core then does 2-clock slots.
- **Divide error** **(MAME)**: vector 0 with the address after the DIV pushed, AW and DW unchanged.
- `v40_pc()` is linear (PS * 16 + IP, 20 bits).

Tests (`test_v40_contract.c`, 28): `zero_budget`, `reset_vector`, `reset`, `two_cores`, `int_accept`, `int_masked`,
`ei_shadow`, `ei_nmi`, `ei_halt`, `sreg_shadow`, `sreg_from_shadow`, `pop_sreg_shadow`, `poll_shadow`,
`prefix_atomic`, `rep_steps`, `rep_irq`, `rep_seg_irq`, `rep_seg_steps`, `halt_int`, `halt_masked_wake`, `halt_nmi`,
`nmi`, `nmi_priority`, `undefined`, `mode8080_fault`, `div_overflow`, `word_order`, `wrap20`. Astra's four probes
(Reply 103) are `ei_shadow` (an INT pending at EI: handler one step later, IP 0407 pushed), `halt_masked_wake` (CLI;
HLT; a masked INT: resumed after the HLT without an acknowledge, then serviced after a later EI and one instruction),
`sreg_from_shadow` (MOV AW,DS0) and `poll_shadow`; the deferral tests take NMI as well as INT. `v40_controls.py`
undoes 28 rules, each failing exactly its tests: among them EI's delay removed, EI's delay put on `m_no_interrupt`
(then `ei_nmi` alone fails), each new deferral removed, the masked release removed, turned into a drop or into an
acceptance, and HALT released at the line. No control is possible for the statics made members (`Mod_RM`,
`parity_table`, `nec_popa_tmp`: the tables are the same in every instance), nor for reset clearing EI's delay (reset
clears IE, and the first instruction spends the delay).

## 13. The 6502 (MOS 6502, NMOS: the Apple II with a Mockingboard)

The core is Fake6502's instructions (v1.1, Mike Chambers, public domain), copied by `extract_6502_machine.py` from
the copy Jayson Smith's EchoTalk vendors (`fake6502/PINNED.txt`), behind our step driver `m6502.c`. Its state is the
core's own (upstream keeps it in globals); a thread-local pointer names the core whose instruction runs, so two
cores run side by side, on different threads too.

- **A step** is the phases of 1: A acceptance (an NMI edge first, else an IRQ level that I does not mask: PC and P
  pushed with B clear and bit 5 set, I set, PC from FFFAh / FFFEh, 7 cycles **(chip)**), B its charge, D the boundary,
  E one instruction, F its cycles (the table's, a page crossed on the reads upstream charges, a branch taken: 1, across
  a page: 2 **(chip; upstream)**).
- **The poll** **(chip)**: the NMOS 6502 polls its interrupt lines before an instruction's last cycle. CLI, SEI and
  PLP change I in their last cycle, so the poll after them still sees the old I: an IRQ CLI unmasks waits one more
  instruction, and CLI; SEI with an IRQ held takes it after SEI. RTI restores I before its poll. Not modelled **(model)**:
  a taken branch that crosses no page delaying the poll, and an interrupt hijacking BRK.
- **Memory-mapped I/O only**: the bus's `in` and `out` are never called.
- **Decimal mode** **(upstream, NOT the chip)**: ADC and SBC with D set adjust the old accumulator and then store the
  binary sum. Counted in `m6502_regs.decimal`, so a board shows its firmware never reaches them.
- **Undocumented opcodes** **(upstream's guesses)**: run as Fake6502 runs them, counted in `m6502_regs.undocumented`
  with the last one's address.
- **Not modelled**: the dummy reads and writes of indexed and read-modify-write instructions (a soft switch read
  twice); no firmware here relies on them.

Tests (`test_m6502_contract.c`, 17): `zero_budget`, `reset`, `two_cores`, `boundary`, `irq_entry`, `irq_masked`,
`cli_delay`, `sei_delay`, `plp_delay`, `rti_at_once`, `irq_level`, `nmi_edge`, `brk`, `jmp_ind_wrap`, `cycles`,
`decimal`, `undocumented`. `m6502_controls.py` undoes 10 rules, each failing exactly its tests: the poll's delay
removed, RTI delayed, I not masking, NMI a level, B set on an interrupt's push, the boundary, the counts, the JMP
($xxFF) page wrap, and a branch across a page.

## 14. The 6809 (Motorola MC6809)

MAME's 6809 (pinned in `mame_m6809/PINNED.txt`) behind our step driver `m6809_mame.cpp`. Motorola's figures below are
from the MC6809 data sheet DS9845-R2 (1983), cited by its printed page.

This core is not built like the others. MAME generates its instructions: `m6809.lst` describes each one cycle by
cycle, and `m6809make.py` turns that into one state machine (`execute_one`) that can stop after any cycle and resume
there in a later time slice. The interrupt test is that machine's entry, and the SYNC and CWAI waits are loops inside
it. So acceptance and suspension inside an instruction are upstream's generated code here, not something a driver
puts round an instruction function, and the 8085's split does not carry over as it is. What marks an instruction's
end is the machine's state stack being empty. The phases of 1 are applied to it this way:

- **A step** **(model, on upstream's machine)**:
  - **A**, unless an instruction is being continued (below): the lines are sampled once -- an NMI's latch, else FIRQ
    unless F, else IRQ unless I -- and at most one is accepted: the stacking, the masks, the vector. `m6809_pc()` is
    the interrupted address, where execution resumes: the PC that is pushed.
  - **B**: its cycles, 19, or 10 for FIRQ **(chip:** Figures 9 and 10, pp.10-11; upstream's count is the same**)**.
  - **D**: `steps` increments, then `boundary`. (The 6809 has no EI shadow: C is empty.)
  - **E**: one whole instruction, from its first byte to its last cycle. A page prefix (10h, 11h) is part of it
    **(chip:** Figure 18 sheet 1, p.22: the fetch loops while the byte is 10h or 11h**)**: no boundary and no
    acceptance come between a prefix and its opcode. The machine does not sample the lines here: a line the board
    raises at D is taken at the next step's A (1).
  - **F**: its cycles.
- **An instruction continued over steps.** A step never leaves the machine suspended inside an instruction for want
  of a budget (MAME does, at a slice's end), with two exceptions. Both continue in the following steps, which skip A
  -- the chip takes no interrupt inside an instruction -- and keep `m6809_pc()` on the instruction's first byte:
  - a **wait** in SYNC or CWAI (below), one cycle a step. The wait's test for a line is the instruction's own and
    runs at E, after the step's boundary **(model:** the chip tests after each wait cycle**)**. So a line the board
    raises at a slot's boundary ends the wait in that same step -- SYNC's last cycle, CWAI's vector -- where 1's rule
    for D would have it wait a step; and a CWAI finds, when its stacking is done, a line raised at its own boundary.
    After a SYNC the acceptance is still the next step's A;
  - a **run of page prefixes** past the step's limit of 64 cycles **(model)**. The longest instruction takes 20
    (SWI2, SWI3, a CWAI that finds its interrupt), so only such a run gets there; without the limit a memory full of
    10h would keep one step, and `m6809_run`, going for ever. The step ends at the limit and the next one goes on
    from that cycle.
- **One acceptance per step** **(model)**. Motorola's Figure 15 (p.15) returns to the interrupt test straight after a
  vector fetch, before the next instruction, and upstream does the same. Here the handler's first instruction always
  runs in the step that accepted. With steady lines the two agree: NMI and FIRQ set both masks, and an IRQ is only
  reached when no unmasked FIRQ is waiting. They differ when a callback changes a line during the acceptance's own
  bus cycles: that line waits for the next step's A.
- **The interrupts** **(chip:** pp.6, 9, Figures 9, 10 and 15**)**:
  - **NMI**: an edge, not maskable; the whole state is stacked with E set in the stacked CC, F and I are set, the
    vector is FFFCh; 19 cycles. It is **armed only once S has been loaded**: "After reset, an NMI will not be
    recognized until the first program load of the hardware stack pointer (S)" (p.9; Figure 15's "Arm NMI").
    **(upstream)** What arms it is LDS, LEAS and TFR r,S, nothing else in the generated machine (EXG r,S does not);
    and an edge that comes before that is dropped, not kept for later: the data sheet says only "not recognized".
    `m6809_regs_set` arms or disarms as its `nmi_armed` says, the host's S standing in for the program's LDS.
  - **FIRQ**: a level, masked by F; **only PC and CC are stacked, with E clear in the stacked CC**; F and I are set;
    the vector is FFF6h; 10 cycles.
  - **IRQ**: a level, masked by I; the whole state is stacked with E set; I is set and F is left, so a FIRQ can
    interrupt its handler; the vector is FFF8h; 19 cycles.
  - **Priority**: NMI, then FIRQ, then IRQ.
  - **RTI** pulls CC and then what its E bit says: PC alone (6 cycles) or the whole state (15) (Table 9, p.29).
  - Not modelled: the cycle the inputs take to be synchronised (a line is seen at the next step's A; a wait sees it
    in the step before which, or at whose boundary, it was raised), and the acknowledge on BA and BS. `irq_ack` is never called: a vector is two reads of memory at
    FFF2h-FFFFh.
- **SYNC** **(chip:** p.20, Figure 17, p.21, and Figure 18 sheet 2, p.23**)**: the step that fetches it is 3 cycles --
  the opcode, the don't-care cycle at the next byte and the first sync-acknowledge cycle -- and leaves the core
  waiting (`m6809_regs.waiting`). Each further step is a slot of one cycle **(model:** a step a cycle, so the board's
  boundary runs while the chip waits**)**. Any line ends it, masked or not -- FIRQ, IRQ, or an armed NMI's edge: the
  slot that finds it is SYNC's last cycle, so SYNC is 4 cycles at least (Table 9: ">= 4"). **Changed from upstream**,
  which tests before the first acknowledge cycle and takes 3 with a line already there; Figure 18 runs that cycle
  first. The step after is an ordinary one: an unmasked interrupt is accepted at its A, the address after the SYNC
  pushed; with its mask set "the processor will clear the sync state and continue processing by executing the next
  in-line instruction" (p.20). Not modelled: the three cycles a level must last to be taken (Figure 17, note 2); the
  slots make no bus access (the bus is three-state).
- **CWAI** **(chip:** Figure 15 and Figure 18 sheet 2**)**: its step ANDs CC with the operand, sets E and stacks the
  whole state: 16 cycles. If an interrupt the new CC does not mask is already there, the vector follows in the same
  step, 20 cycles in all (Table 9: ">= 20"). Otherwise the step ends with the first wait cycle, 17 in all, and each
  further step is a one-cycle slot **(model)** until one finds such an interrupt: that step is the vector alone, 4
  cycles, with nothing more stacked; F and I are set, for an IRQ only I. So **a CWAI's acceptance happens at E**, the
  one exception to A; the handler's first instruction is the next step. A FIRQ that ends a CWAI finds the whole
  state on the stack with E set, so its RTI is the 15-cycle one. A masked line does not end a CWAI.
- **Reset** **(chip:** Figure 15's RESET sequence; upstream**)**: DP = 0, F and I set, a latched NMI dropped and NMI
  disarmed, the free run ended; the other registers keep their values. PC is read from FFFEh-FFFFh at once -- MAME
  leaves that to its first slice, which would overwrite a PC the host set meanwhile -- so `m6809_create` and
  `m6809_reset` read the bus. The reset sequence's own cycles are not counted. The lines are kept, as 9 says.
- **Memory-mapped I/O only**: the bus's `in`, `out` and `irq_ack` are never called and may be NULL.
- **The bus** **(upstream)**: every cycle but a wait's slot is one bus access, the instruction's own or one of the
  chip's don't-care cycles, which upstream performs as real reads: at FFFFh (Motorola's VMA cycle, p.6) or at an
  address of upstream's choice, not always Figure 18's (the sixth cycle of DEC extended reads the next program byte
  where Example 2, p.20, shows FFFFh). A board must not count on those addresses. What Motorola does specify holds:
  CLR, INC, DEC, the shifts and the like read their byte before they write it (Figure 18 sheet 5, p.26), and a store
  does not. Only opcode bytes -- an instruction's first byte and the byte after a prefix -- come through `bus.fetch`.
  `m6809_cycles()` in a callback is start-stamped as for every core (7, 8): the step's start during A, start +
  acceptance during E.
- **Cycle counts** **(upstream; the chip's where tested)**: E-clock cycles, one per bus cycle (on an MC6809 E is the
  crystal divided by 4, p.12). The counts are upstream's, built cycle by cycle; the tests hold 103 instruction forms to
  Table 9 (pp.29-30), Figure 19 (pp.31-33) and Table 2 (p.17), the interrupts to Figures 9 and 10, RTI, SYNC and CWAI
  to Table 9. Every form tested was Motorola's. The forms not in those tests are upstream's, unchecked here.
- **Undefined opcodes** **(upstream's own choices, NOT the chip's as far as Motorola says)**: Table 9 leaves 33 first
  bytes unused, defines 38 opcodes after 10h and 9 after 11h, and says "All unused opcodes are both undefined and
  illegal" (p.30). Upstream runs every one some way (its history says after a description of the chip's undocumented
  behaviour; nothing here checked that), and never reports one: its page-1 switch has a case for all 256 bytes. So
  the core counts them itself, by Table 9's map, in `m6809_regs.undefined` -- one for each unused byte fetched as an
  opcode -- with the instruction's address. As tested: 01h runs as NEG; an unused byte after a prefix is dropped and
  the byte after it runs as a page-1 opcode, in the same step; a second prefix is ignored (10h 10h 8Eh is LDY, the
  loop of Figure 18) and counted, Table 9 having no such opcode. 14h, 15h and CDh start upstream's **free run**
  (`m6809_regs.free_run`), its model of a test mode of the chip: every step from then on is one fetch of the next
  address, nothing runs, until reset.
- **A host's calls**: `m6809_regs_set` between steps writes every register and leaves any wait or cut instruction, so
  the next step starts a new instruction at the PC given; `m6809_next_pc` is the PC now, for a run-until test. A host
  calls a subroutine by setting PC and S with its own return address on the stack, and stepping until
  `m6809_next_pc` is that address (`call_return`).
- **`m6809_regs.fault`**: upstream's `fatalerror` and `assert` (a state or an operand mode its own machine never
  sets) set it instead of throwing; the core then does one-cycle slots until reset. No opcode reaches it
  (`every_opcode`).
- **Not modelled**: the HALT, DMA/BREQ and MRDY inputs (no clock stretching, no bus grant), the LIC, BA and BS outputs,
  and the MC6809E's pins.

Tests (`test_m6809_contract.c`, 29, every program hand-assembled in the test): `zero_budget`, `reset`, `two_cores`,
`boundary`, `call_return`, `regs_set_wait`, `page_prefix`, `irq_entry`, `firq_entry`, `rti`, `masks`, `priority`,
`irq_level`, `nmi_edge`, `nmi_armed`, `boundary_irq`, `sync`, `cwai`, `cwai_firq`, `wait_nmi`, `wait_boundary`,
`undefined`, `free_run`, `prefix_run`, `every_opcode` (every first byte, alone and after each prefix, with four
operand patterns: each step returns having spent 1 to 64 cycles), `cycles`, `cycles_indexed`, `cycles_stack`,
`bus_cycles`. `m6809_controls.py` undoes 53 rules, each failing exactly its tests: of the driver (acceptance at A and
not inside an instruction, the step limit, the two PCs and `next_pc`, `regs_set`, reset's vector fetch, the counting
and its map, the waiting state, a zero budget, the boundary, its place before E and the step count), of the shim
(opcode bytes through `fetch`), of the extraction's named changes (the machine sampling at A only, SYNC's first
acknowledge cycle, the one-cycle slot and its cost, the three fetch counts) and of upstream (NMI's arming by each of
its three instructions and its edge, the levels, the masks, the priority, NMI's masks, FIRQ's short frame and its E,
IRQ leaving F, RTI by E, CWAI's frame, its masks and its use of the NMI edge, reset's four effects, each core's own
registers, every operand mode, CLR's read, and three cycle counts standing for the rest). No control: the other cycle
counts one by one, and upstream's choices for the unused opcodes, which `undefined` pins as they are.

## 15. The Z80 (Zilog Z80)

MAME's Z80 (pinned in `mame_z80/PINNED.txt`) behind our step driver `z80_mame.cpp`. Zilog's figures below are from
the Z80 CPU User Manual UM008011-0816, cited by its sections, figures and tables.

Like the 6809's, this core's instructions are generated: `z80.lst` describes each one step by step and `z80make.py`
turns that into the whole body of `execute_run`, one loop. Its first state looks at the interrupt lines, takes one,
fetches an opcode and dispatches; every instruction ends by jumping back there, and MAME stays in the loop for as
long as its time slice lasts. A prefix jumps on to its opcode without passing that state. The extraction makes four
named changes at that first state, each marked `CHANGED` where it is made (twice: the state is in the generated text
once for the fast loop and once for resuming), so that the loop serves a step:

1. **A pass ends when it comes back to the state.** The driver lets a pass in once (`m_drv_enter`); the second
   arrival returns. Why: upstream runs on from one instruction to the next until its slice is spent, and a step must
   be one instruction, with D between any two.
2. **The lines are looked at in an acceptance pass only** (`m_drv_accept`). Why: an instruction's own pass comes
   through the same state after D, and a line the board raised there belongs to the next step's A (1), not to this
   instruction's start.
3. **The acceptance pass returns after the look, taken or not, before the fetch.** Why: upstream goes from the
   acceptance straight into the handler's first instruction, which leaves no place for B, C and D.
4. **The acknowledge cycle's byte comes from `cpu_bus.irq_ack`.** Why: upstream asks its daisy chain or the
   framework's callback, neither of which exists here.

Two more named changes lie elsewhere: an NMI edge is latched before the first step too (the input line's code), and
the four bus accessors are not virtual (no behaviour: the plain Z80 only). The driver itself adds one rule, reset's
mode 0. The phases of 1 are applied this way:

- **A step** **(model, on upstream's loop)**:
  - **A**, unless an instruction is being continued (below): the lines are looked at once, by upstream's own test --
    a latched NMI edge, else INT while IFF1 is set and the instruction before was not EI -- and at most one is
    accepted: the return address pushed, the acknowledge byte read (INT only), the PC set. A halted core leaves
    HALT. `z80_pc()` is the interrupted address, where execution resumes: the PC that is pushed; for a halted core,
    the address after its HALT.
  - **B**: its T-states (below).
  - **C**: the EI delay ends. Upstream clears its flag at the head of the fetch, which here is the head of E's pass,
    after D; no callback can tell, nothing in between reads it.
  - **D**: `steps` increments, then `boundary`.
  - **E**: one whole instruction from its first byte to its last T-state, every prefix part of it **(chip:** an
    interrupt is accepted "at the end of any instruction", Interrupt Request/Acknowledge Cycle**)**: no boundary and
    no acceptance come between a prefix and its opcode. Or one HALT slot (below). The loop does not look at the lines
    here: a line the board raises at D is taken at the next step's A (1).
  - **F**: its T-states.
- **A block instruction is one repetition a step** **(chip; upstream)**: LDIR, LDDR, CPIR, CPDR, INIR, INDR, OTIR
  and OTDR move PC back onto themselves while they go on (21 T a repetition, 16 the last: their descriptions), so
  each repetition is an instruction of its own, `z80_pc()` on it each time, and an interrupt between two pushes the
  instruction's own address.
- **An instruction continued over steps** **(model)**: a run of prefixes that never reaches an opcode (DDh DDh
  DDh ...: upstream fetches each, 4 T, and looks at the next) would keep one step, and `z80_run`, going for as long
  as the bytes last. A step is given 64 T-states; the longest instruction takes 23 (EX (SP),IX; the shifts, INC and
  DEC, SET and RES on (IX+d)), so only such a run gets there. It is cut at the limit as MAME cuts at a slice's end,
  and the next step goes on from that T-state: it skips A -- the chip takes no interrupt after a prefix -- and keeps
  `z80_pc()` on the instruction's first byte.
- **INT** **(chip:** Interrupt Request/Acknowledge Cycle and Figure 9; Interrupt Response; Table 1**)**: a level,
  taken while IFF1 is set; dropped before its acceptance, it is not taken. Acceptance clears IFF1 and IFF2, counts
  in R (the acknowledge is an M1 cycle) and reads the data bus in every mode: `irq_ack(ctx, Z80_INT, 0)`, once,
  FFh without an answer. The acknowledge cycle's two wait states are in every count.
  - **Mode 1**: PC pushed, PC = 0038h, the byte ignored; 13 T ("two more than normal" for a restart: CPU Response,
    Mode 1; RST is 11).
  - **Mode 2**: PC pushed, then PC read from the table at I * 256 + the byte; 19 T ("seven to fetch the lower eight
    bits from the interrupting device, six to save the program counter, and six to obtain the jump address": CPU
    Response, Mode 2). **(upstream)** all eight bits of the byte are used; the manual wants bit 0 clear.
  - **Mode 0** **(upstream, NOT the chip in full)**: the chip executes whatever instruction the device puts on the
    bus, in two T-states more than its own. Upstream knows five and, unlike the Z180's (4), takes them in the
    acceptance, at A, not as the step's instruction: RST (13 T), CALL nn (19 T, its address from acknowledge bytes 1
    and 2, low byte first), JP nn (12 T, likewise), EI (6 T) and NOP -- for which it charges 2 T where the manual's
    rule gives 6. Any other byte does nothing in 2 T: nothing pushed, IFF1 and IFF2 cleared. After any of them the
    step still runs an instruction at E, so a step always spends at least 4 T-states and `z80_run` returns.
- **NMI** **(chip:** Nonmaskable Interrupt Response and Figure 10; the NMI pin, "negative edge-triggered"; Table
  1**)**: an edge -- one acceptance for each time the line goes from released to asserted; a held line gives no
  second one -- taken whatever IFF1 is, before INT. PC pushed, PC = 0066h, IFF1 cleared and IFF2 left, so RETN (14
  T), which copies IFF2 into IFF1, restores the enable; R counted; 11 T (as a restart's: CPU Response). No
  acknowledge byte is read. **Changed from upstream**: an edge is latched whenever it comes, before the first step
  too; MAME drops one that arrives at its machine's time 0.
- **EI** **(chip:** Interrupt Enable/Disable: "any pending interrupt request is not accepted until after the
  instruction following EI is executed"**)**: the step after an EI skips INT at its A; EI then DI takes none.
  **(upstream)** NMI is not delayed: one that waits at EI's boundary is taken before the next instruction. The
  manual's sentence does not say which interrupts it means; nothing here measured a chip.
- **HALT** **(chip:** HALT Exit and Figure 11**)**: the HALT is 4 T and leaves PC after it. Each further step is a
  slot of 4 T: the chip's forced NOP, an M1 fetch at the address after the HALT whose byte is ignored, R counted;
  `z80_pc()` stays on the HALT during the slot. An NMI, or an INT while IFF1 is set, ends it at a step's A, the
  address after the HALT pushed; with IFF1 clear an INT leaves the core halted. A line raised at a slot's boundary
  is taken at the next step.
- **R** **(chip:** Memory Refresh (R) Register**)**: seven bits count M1 cycles -- one an unprefixed instruction, two
  with a prefix (the DD CB d xx forms two as well: their last byte is not fetched by an M1), one an acceptance, one
  a HALT slot; bit 7 is what LD R,A put there.
- **Reset** **(chip:** the RESET pin: it "resets the interrupt enable flip-flop, clears the Program Counter and
  registers I and R, and sets the interrupt status to Mode 0"**)**: PC = 0, I = R = 0, IFF1 = IFF2 = 0, mode 0, out
  of HALT, the EI delay and a latched NMI edge dropped, any cut instruction left; the other registers keep their
  values. **Changed from upstream**, whose reset leaves the interrupt mode. The reset's own T-states are not
  counted. The lines are kept, as 9 says: a held INT is sampled again, a held NMI line is no new edge. A new core's
  registers are upstream's power-on values: IX = IY = FFFFh, F = 40h, the rest 0.
- **Ports** **(chip:** the IN and OUT descriptions: the accumulator, or B, "on the top half (A8 through A15) of the
  address bus"**)**: `in` and `out` get the 16 bits the chip drives -- BC for IN r,(C), OUT (C),r and the block
  I/O, n with A above it for IN A,(n) and OUT (n),A. A board that decodes eight lines masks the port. There is no
  on-chip I/O: every port reaches the bus.
- **The bus** **(upstream)**: every M1 cycle's byte comes through `bus.fetch` -- an instruction's first byte, each
  prefix and the opcode after it, a HALT slot's ignored byte; everything else through `read` and `write`, the fourth
  byte of a DD CB d xx form included. The accesses are in the chip's order but not at their own T-states:
  `z80_cycles()` in a callback is start-stamped as for every core (7, 8) -- the step's start during A, start +
  acceptance during E. The refresh address and the cycles without MREQ are not put on the bus.
- **T-states** **(upstream; the chip's where tested)**: built from bus cycles of 4 (M1), 3 (memory) and 4 (I/O)
  T-states and the extra ones `z80.lst` adds. The tests hold 116 instruction forms to the manual's descriptions ("M
  Cycles, T States"), conditions met and not met, and the acceptances to the figures above. Every form tested was
  Zilog's. The forms not in those tests are upstream's, unchecked here.
- **Undocumented opcodes and flags** **(upstream's)**: every opcode runs as upstream runs it -- the IX and IY
  halves, SLL, the DD CB forms that also load a register, a DDh or FDh before an instruction that does not use HL
  (the prefix is 4 T and the instruction runs unprefixed, one step), the EDh opcodes Zilog leaves undefined (8 T,
  nothing done). Bits 3 and 5 of F, WZ and the SCF/CCF rule are upstream's. What holds them to a chip: zexdoc and
  zexall, the CP/M instruction exercisers, each pass all 67 of their groups on this core (`test_z80_zex.c`; zexdoc
  masks the undocumented flag bits, zexall compares every bit of F; each group's CRC is, in their source's words,
  "an expected value that was found empirically on a real Z80"). **Which chip:** upstream says it models a
  ZiLOG NMOS part: OUT (C),0 writes 00h (a CMOS Z80 writes FFh, by upstream's own note), bits 3 and 5 after SCF
  and CCF follow its rule for that part, and the LD A,I / LD A,R parity glitch of the NMOS part is off. The
  exercisers cannot tell an NMOS from a CMOS Z80 here: neither runs an I/O instruction, and no CMOS chip was
  measured. A board with a CMOS Z80 (a Z84C00) should check whether its firmware runs OUT (C),0 or reads bits 3 and
  5 after SCF or CCF; none has been checked yet.
- **A host's calls**: `z80_regs_set` between steps writes every register of `z80_regs` and leaves HALT, the EI delay
  and any cut instruction, so the next step starts a new instruction at the PC given; `z80_next_pc` is the PC now,
  for a run-until test (`call_return`). WZ and the SCF/CCF latch are not in `z80_regs`: a host cannot set them.
- **`z80_regs.fault`**: upstream's two `assert`s (a state number its own loop never sets) set it instead of
  aborting; the core then does 4-T slots until reset. No opcode reaches it (`every_opcode`).
- **Untested**: a bus whose `fetch` or `irq_ack` is NULL (the defaults of 9 are coded: `read`, and FFh), and the
  fault's slots.
- **Not modelled**: WAIT and BUSREQ (upstream's code for them is in the generated file, but `cpu.h` has no such
  lines), the M1, RFSH, HALT and BUSACK outputs, the daisy chain (RETI tells no device), and the instant inside an
  instruction at which the chip samples its lines: a line is seen at the next step's A.

Tests (`test_z80_contract.c`, 27, every program hand-assembled in the test): `zero_budget`, `reset`, `two_cores`,
`boundary`, `call_return`, `regs_set`, `prefix`, `int_im1`, `int_im2`, `int_im0`, `int_masked`, `int_level`,
`ei_delay`, `ei_nmi`, `nmi_edge`, `nmi_first`, `priority`, `halt`, `halt_masked`, `boundary_irq`, `block`,
`prefix_run`, `io`, `r_register`, `undocumented`, `every_opcode` (every first byte, alone and after CBh, DDh, EDh,
FDh, DD CB d and FD CB d, with four operand patterns: each step returns having spent 4 to 64 T-states), `cycles`.
`z80_controls.py` undoes 47 rules, each failing exactly its tests: of the driver (acceptance at A and not inside an
instruction, the step limit, the two PCs and a HALT slot's, `next_pc`, `regs_set`'s four effects, F and R's bit 7 in
`regs_get`, reset's mode and counts, a zero budget, the boundary, its place before E and the step count), of the
shim (M1 bytes through `fetch`, the acknowledge byte and mode 0's two more, a port's 16 bits), of the extraction's
named changes (a pass ending at the boundary, the lines sampled at A only, the acceptance ending before the fetch, an
NMI edge before the first step) and of upstream (NMI's edge, its priority, its IFF1 and its 11 T, RETN, INT's level,
its mask, its two flip-flops, EI's delay, the acknowledge's wait states and its count in R, reset's four effects,
each core's own registers, and the three bus cycles' lengths standing for the T-states). With `--zex` it runs the
exercisers' controls instead: a documented result undone (DAA), which zexdoc and zexall must both report, and bits
3 and 5 of F after CP, which zexall alone must. No control: the T-states one by one, upstream's choices for the
undocumented opcodes, which `undocumented` pins as they are, and `fault`.
