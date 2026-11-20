// i8085_mame.cpp -- cpu.h's 8085 on MAME's 8085 core: the CORRECTED path (CONTRACT.md 1-2, 4-7, 9).
//
// One translation unit: MAME's code (i8085_mame_machine.cpp, generated) is included here, so its helpers are seen by
// the step driver and inline into it.  Build this file; never i8085_mame_machine.cpp alone.
//
// The driver is ours.  It follows MAME's execute_run (i8085.cpp, the revision in mame_i8085/PINNED.txt) one step at
// a time, in the contract's phases:
//   A  execute_run's entry test at every step: TRAP whenever it is pending, the maskable sources only outside the
//      EI shadow (MAME's check_for_interrupts sets the priority and the masks).  A vectored acceptance pushes and
//      jumps; INTR is only accepted (IE cleared, HALT left): its instruction comes at E.  i8085_pc() is the
//      interrupted address.
//   B  the acceptance T-states (12: the extraction's I8085_ACCEPT_T, from Intel).
//   C  the EI shadow ends.
//   D  steps++, the saved PC, bus->boundary.
//   E  one instruction; or the injected one after an INTR acceptance, its bytes from the acknowledge; or one HALT
//      slot of 4 T-states (CONTRACT.md 6, model: MAME re-fetches the HLT at 5 T each time).
//   F  its T-states.  (The 8085 has no on-chip timers, serial port or DMA: nothing else is clocked, and G is empty.)
//
// Differences from MAME, each deliberate (the generated file marks those made there 'CHANGED'):
//   - Interrupts are sampled at every step's A (MAME: at execute_run's entry and when the EI shadow runs out, so a
//     line raised inside a slice waits for the next slice).
//   - Acceptance of TRAP and RST 5.5/6.5/7.5 is 12 T-states (Intel's RST on the 8085; MAME: 11, the 8080's).
//   - INTR: the instruction is injected at E (CONTRACT.md 4) with bus.irq_ack(ctx, I8085_INTR, n), n from 0 at each
//     acceptance; MAME reads and executes it inside the acceptance.  Its PC is held, a conditional branch not taken
//     included (MAME moves the PC by 2 there).
//   - SIM does not accept an interrupt inside its own step (MAME calls check_for_interrupts there); what it unmasks
//     is taken at the next step's A.
//   - A HALT slot is 4 T-states without a fetch (MAME re-executes the HLT, 5 T-states).
//   - TRAP and the RSTs make no acknowledge callback (no bus read happens for an internal RESTART).
// Known limits: the undocumented 8085 flags (V, K) are MAME's; the core is not bus-cycle accurate (an access inside
// an instruction is not placed at its own T-state; CONTRACT.md 7-8).
#include "i8085_mame_machine.cpp"

#include <new>

// ---- the driver's pieces, as members ----------------------------------------------------------------------------

int i8085a_cpu_device::drv_accept()
{
    m_icount = 0;
    if (m_trap_pending || m_after_ei == 0)   // execute_run's entry test: TRAP is never delayed by EI
        check_for_interrupts();
    return -m_icount;
}

// Phase E after an INTR acceptance (CONTRACT.md 4): the step's one instruction comes from the acknowledge -- byte 0
// the opcode, then any operands (MAME's read_arg/read_arg16 read the acknowledge while m_in_acknowledge is set, and
// do not move the PC) -- and does its own control transfer: an RST or CALL pushes the interrupted PC; a NOP pushes
// nothing.  No ordinary instruction follows in this step.  T-states: the instruction's own (Intel: "The INA cycle is
// identical to an OF cycle" except INTA for RD; the CALL's two further INA cycles are three states each, as its
// memory reads: MCS-80/85 Family User's Manual, section 2.3.4, Figures 2-17/2-18).
int i8085a_cpu_device::drv_injected_instruction()
{
    m_inject_pending = false;
    m_in_inta_func.n = 0;
    m_in_acknowledge = true;
    execute_one(read_inta());
    m_in_acknowledge = false;
    return -m_icount;
}

int i8085a_cpu_device::drv_instruction()
{
    m_icount = 0;
    if (m_inject_pending)
        return drv_injected_instruction();
    if (m_halt)
        return 4;                         // a HALT slot (CONTRACT.md 6)
    m_in_acknowledge = false;             // as execute_run, before each fetch
    execute_one(read_op());
    return -m_icount;
}

// ---- cpu.h ------------------------------------------------------------------------------------------------------

struct i8085 {
    cpu_bus bus;
    i8085a_cpu_device *dev;
    uint64_t cycles, steps;
    uint32_t pc;
};

extern "C" {

i8085 *i8085_create(const cpu_bus *bus, double clock_hz)
{
    (void)clock_hz;                       // the core counts T-states; the board turns them into time
    i8085 *c = new (std::nothrow) i8085();
    if (!c)
        return nullptr;
    c->bus = *bus;
    c->dev = new (std::nothrow) i8085a_cpu_device(&c->bus);
    if (!c->dev) {
        delete c;
        return nullptr;
    }
    c->dev->init_state();                 // MAME's power-on values and this instance's tables
    i8085_reset(c);
    return c;
}

void i8085_destroy(i8085 *c)
{
    if (!c)
        return;
    delete c->dev;
    delete c;
}

// Reset (CONTRACT.md 9): MAME's device_reset -- PC 0, IE off, all three masks set, the RST 7.5 latch and a pending
// TRAP cleared (Intel: RESET IN resets both flip-flops) -- with the lines kept as they are: a level still held is
// sampled again at the next A; a TRAP line still high is no new edge.
void i8085_reset(i8085 *c)
{
    c->dev->device_reset();
    c->dev->m_inject_pending = false;
    c->dev->m_in_acknowledge = false;
    c->cycles = c->steps = 0;
    c->pc = 0;
}

int i8085_step_slice(i8085 *c, uint64_t budget, int *accepted_only)
{
    if (accepted_only)
        *accepted_only = 0;
    if (!budget)
        return 0;
    i8085a_cpu_device &d = *c->dev;
    // A's callbacks (the stack writes) see the interrupted instruction's address: where execution resumes.  A halted
    // core's PC rests on its HLT, so that address is past it.  D then sets the saved instruction-start PC.
    c->pc = (uint32_t)((d.m_PC.w.l + (d.m_halt ? 1 : 0)) & 0xffff);
    int acc = d.drv_accept();             // A
    c->cycles += (uint64_t)acc;           // B
    d.m_after_ei = 0;                     // C

    // A board may end a slice at acceptance, as the Python Accent-PC host does. Do not execute the vector's
    // instruction and carry only its cost: that sends I/O and changes registers/memory one slice too early.
    // There is no pending instruction to replay; the next call samples its then-current interrupt lines at A.
    if (acc && (uint64_t)acc >= budget) {
        if (accepted_only)
            *accepted_only = 1;
        return acc;
    }

    c->steps++;                           // D
    c->pc = d.m_PC.w.l;
    if (c->bus.boundary)
        c->bus.boundary(c->bus.ctx, c->pc);

    int ins = d.drv_instruction();        // E
    c->cycles += (uint64_t)ins;           // F
    return acc + ins;
}

int i8085_step(i8085 *c)
{
    return i8085_step_slice(c, UINT64_MAX, nullptr);
}

uint64_t i8085_run(i8085 *c, uint64_t budget)
{
    uint64_t done = 0;
    while (done < budget)
        done += (uint64_t)i8085_step(c);
    return done;
}

void i8085_set_irq(i8085 *c, int line, int asserted)
{
    if (line < I8085_INTR || line > I8085_TRAP)
        return;
    // cpu.h's INTR..RST75 are MAME's input numbers 0-3; TRAP is MAME's NMI
    c->dev->execute_set_input(line == I8085_TRAP ? I8085_TRAP_LINE : line, asserted ? ASSERT_LINE : CLEAR_LINE);
}

uint64_t i8085_cycles(const i8085 *c) { return c->cycles; }
uint64_t i8085_steps(const i8085 *c) { return c->steps; }
uint32_t i8085_pc(const i8085 *c) { return c->pc; }

// No side effects (CONTRACT.md 7): `im` is the RIM view -- masks, the RST 7.5 latch, the live RST 5.5/6.5 lines, IE
// as it is now -- without reading SID (bit 7 is 0) and without consuming the post-TRAP IE that the next RIM reports.
void i8085_regs_get(const i8085 *c, i8085_regs *out)
{
    const i8085a_cpu_device &d = *c->dev;
    out->af = d.m_AF.w.l; out->bc = d.m_BC.w.l; out->de = d.m_DE.w.l; out->hl = d.m_HL.w.l;
    out->sp = d.m_SP.w.l; out->pc = d.m_PC.w.l;
    uint8_t im = (uint8_t)(d.m_im & ~(IM_SID | IM_I65 | IM_I55));
    if (d.m_irq_state[I8085_RST65_LINE]) im |= IM_I65;
    if (d.m_irq_state[I8085_RST55_LINE]) im |= IM_I55;
    out->im = im;
    out->halted = d.m_halt ? 1 : 0;
}

}  // extern "C"
