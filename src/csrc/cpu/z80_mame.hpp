// z80_mame.hpp -- what MAME's Z80 core compiles against, without the MAME framework.
//
// MAME's Z80 (z80.cpp, z80.h and the instructions its build generates from z80.lst; BSD-3-Clause, copyright-holders:
// Juergen Buchmueller, Andrei I. Holub) is copied by extract_z80_machine.py into z80_mame_machine.cpp, as the class
// `z80_device` built on the shim below.  This header replaces what the framework provided:
//
//   MAME                                        here
//   emucore.h's PAIR16, u8 ... s8               the same names, plain (a little-endian host is required)
//   m_data (memory_access<16,...>::specific)    z80_data: cpu_bus.read / write: operands' data, the stack, the
//                                               vector table of interrupt mode 2
//   m_opcodes (...::cache, the opcodes space)   z80_opcodes: cpu_bus.fetch (or read without one): every M1 cycle's
//                                               byte -- an instruction's first byte, each prefix (CBh, DDh, EDh,
//                                               FDh) and the opcode after it, and a HALT slot's ignored fetch.  Not
//                                               the fourth byte of a DD CB d xx form: that is a plain read, as on
//                                               the chip (no M1 there)
//   m_args (...::cache)                         z80_args: cpu_bus.read: the bytes after the opcode
//   m_io (...::specific)                        z80_io: cpu_bus.in / out, the port's 16 bits as the chip drives them
//   devcb_write_line m_irqack_cb, m_halt_cb,    z80_line_cb: unconnected outputs (the acknowledge is irq_ack below;
//   m_busack_cb                                 HALT is read with z80_regs_get)
//   devcb_write8 m_refresh_cb, m_nomreq_cb      z80_bus_cb: unset, so upstream skips them (no refresh address, no
//                                               cycles without MREQ on the bus)
//   the daisy chain (z80daisy.h) and            drv_irq_ack(): the acknowledge cycle's byte from cpu_bus.irq_ack,
//   standard_irq_callback(line, pc)             packed for interrupt mode 0 as upstream expects (below); no chain,
//                                               so RETI tells no device
//   access_to_be_redone()                       false: no memory access is ever cut and repeated
//   LOGMASKED, logerror, debugger_*_hook        nothing
//   assert(...)                                 drv_fatal() and a return from execute_run: z80_regs.fault.  Upstream
//                                               asserts where its state number names no state; nothing in Z80 code
//                                               gets there (the generated machine sets every number itself)
//
// It also holds the step driver's own state (z80_mame.cpp), which the changed lines of the generated file use.
//
// Licence: this header is ours (MIT); the class it serves is MAME's (BSD-3-Clause; mame_z80/LICENSE-BSD-3-Clause.txt).
#ifndef SSI263_Z80_MAME_HPP
#define SSI263_Z80_MAME_HPP

#include <cstdint>
#include <utility>

#include "cpu.h"

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "PAIR16 is laid out here for a little-endian host"
#endif

// ---- MAME's basic types (emu/emucore.h), little-endian layout ---------------------------------------------------
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;

union PAIR16 {
    struct { uint8_t l, h; } b;
    struct { int8_t l, h; } sb;
    uint16_t w;
    int16_t sw;
};

enum { CLEAR_LINE = 0, ASSERT_LINE = 1 };
enum { INPUT_LINE_IRQ0 = 0, INPUT_LINE_NMI = 32 };   // as MAME's

#define LOG_INT 0
#define LOG_UNDOC 0
#define LOGMASKED(...) ((void)0)            // its arguments are not evaluated (illegal_1's would read the bus)
#define logerror(...) ((void)0)
#define debugger_wait_hook() ((void)0)
#define debugger_instruction_hook(a) ((void)0)
#undef assert
#define assert(x) do { if (!(x)) { drv_fatal(); return; } } while (0)   // both of upstream's are in execute_run

// The short names the instructions are written in (PC, A, HL, CF, TDAT8 ...) are NOT defined here: the extraction
// copies them into z80_mame_machine.cpp, right after its #include of this header, from the BSD-3-Clause z80.cpp
// that last held them (mame_z80/PINNED.txt).  Nothing in this header uses them, and the stand-ins below must not:
// they are compiled before those names exist.

// ---- the framework's stand-ins ----------------------------------------------------------------------------------
struct z80_line_cb {                        // m_irqack_cb, m_halt_cb, m_busack_cb: unconnected
    void operator()(int) const {}
};

struct z80_bus_cb {                         // m_refresh_cb, m_nomreq_cb: unset
    bool isunset() const { return true; }
    void operator()(u32, u8, u8) const {}
};

struct z80_data {                           // m_data: memory
    const cpu_bus *bus;
    u8 read_interruptible(u16 a) const { return bus->read(bus->ctx, a); }
    void write_interruptible(u16 a, u8 v) const { bus->write(bus->ctx, a, v); }
};

struct z80_opcodes {                        // m_opcodes: the M1 cycles' bytes
    const cpu_bus *bus;
    u8 read_byte(u16 a) const { return bus->fetch ? bus->fetch(bus->ctx, a) : bus->read(bus->ctx, a); }
};

struct z80_args {                           // m_args: the bytes after the opcode
    const cpu_bus *bus;
    u8 read_byte(u16 a) const { return bus->read(bus->ctx, a); }
};

struct z80_io {                             // m_io: ports, 16 address bits
    const cpu_bus *bus;
    u8 read_interruptible(u16 p) const { return bus->in(bus->ctx, p); }
    void write_interruptible(u16 p, u8 v) const { bus->out(bus->ctx, p, v); }
};

// The base of MAME's class here, in place of cpu_device and z80_daisy_chain_interface: the bus and the step
// driver's state.
struct z80_shim {
    z80_data m_data;
    z80_opcodes m_opcodes;
    z80_args m_args;
    z80_io m_io;
    z80_line_cb m_irqack_cb, m_halt_cb, m_busack_cb;
    z80_bus_cb m_refresh_cb, m_nomreq_cb;

    // the step driver's (z80_mame.cpp)
    bool m_drv_enter;                       // this pass may start at an instruction boundary (once)
    bool m_drv_accept;                      // this pass is a step's acceptance (A): the lines are looked at
    bool m_drv_fault;                       // upstream's assert

    explicit z80_shim(const cpu_bus *bus)
        : m_data{bus}, m_opcodes{bus}, m_args{bus}, m_io{bus}, m_drv_enter(false), m_drv_accept(false),
          m_drv_fault(false) {}

    bool daisy_chain_present() const { return false; }
    int daisy_update_irq_state() { return CLEAR_LINE; }
    void daisy_call_reti_device() {}
    bool access_to_be_redone() const { return false; }
    void drv_fatal() { m_drv_fault = true; }

    // The data bus during the interrupt acknowledge cycle (Zilog's Figure 9), as upstream wants it in
    // m_tmp_irq_vector: one byte -- the vector's low half in mode 2, the instruction in mode 0, ignored in mode 1 --
    // or, for the two 3-byte instructions upstream knows in mode 0, CALL (CDh) and JP (C3h), the opcode in bits
    // 16-23 and the address in bits 0-15, its low byte acknowledge byte 1 and its high byte byte 2.
    int drv_ack_byte(int n) const
    {
        int v = m_data.bus->irq_ack ? m_data.bus->irq_ack(m_data.bus->ctx, Z80_INT, n) : -1;
        return v < 0 ? 0xff : v & 0xff;
    }
    int drv_irq_ack(int im) const
    {
        int op = drv_ack_byte(0);
        if (im == 0 && (op == 0xcd || op == 0xc3)) {
            int lo = drv_ack_byte(1);
            return op << 16 | drv_ack_byte(2) << 8 | lo;
        }
        return op;
    }
};

#endif
