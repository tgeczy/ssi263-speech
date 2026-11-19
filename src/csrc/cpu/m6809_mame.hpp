// m6809_mame.hpp -- what MAME's 6809 core compiles against, without the MAME framework.
//
// MAME's 6809 (m6809.cpp, m6809.h, m6809inl.h and the state machine its build generates from m6809.lst; BSD-3-Clause,
// copyright-holders: Nathan Woods) is copied by extract_m6809_machine.py into m6809_mame_machine.cpp, as the class
// `m6809_base_device` built on the shim below.  This header replaces what the framework provided:
//
//   MAME                                        here
//   emucore.h's PAIR16, LSB_FIRST, BIT()        the same names, plain (a little-endian host is required)
//   m_mintf (memory_interface: the program      m6809_memory: the cpu_bus callbacks, 16-bit addresses.  read_opcode
//   space, its caches, the opcodes space)       is cpu_bus.fetch (or read without one): the first byte of an
//                                               instruction and the byte after a page prefix.  Everything else is
//                                               cpu_bus.read / write: operands, data, the stack, the vectors, and the
//                                               chip's don't-care cycles, which upstream performs as real reads
//   devcb_write_line m_lic_func,                unconnected outputs: LIC and the sync acknowledge are not signalled
//   m_syncack_write_func
//   devcb_read8 m_vector_read_func              unset: a vector is two reads of memory (FFF2h-FFFFh), not a callback
//   standard_irq_callback(line, pc)             nothing: the 6809 reads no acknowledge byte
//   UNEXPECTED, ATTR_FORCE_INLINE               the compiler's own, or nothing
//   LOGMASKED, logerror, debugger_*_hook        nothing
//   fatalerror(...), assert(...)                drv_fatal(): m6809_regs.fault.  Upstream throws; nothing in 6809
//                                               code reaches them (a state number or an addressing mode that the
//                                               generated machine itself never sets)
//
// It also holds the step driver's own state (m6809_mame.cpp), which the changed lines of the generated file use.
//
// Licence: this header is ours (MIT); the class it serves is MAME's (BSD-3-Clause; mame_m6809/LICENSE-BSD-3-Clause.txt).
#ifndef SSI263_M6809_MAME_HPP
#define SSI263_M6809_MAME_HPP

#include <cstdint>

#include "cpu.h"

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "PAIR16 and MAME's M6809Q are laid out here for a little-endian host"
#endif
#define LSB_FIRST 1                         // MAME's name for that: M6809Q's layout in the generated file

// ---- MAME's basic types (emu/emucore.h), little-endian layout ---------------------------------------------------
union PAIR16 {
    struct { uint8_t l, h; } b;
    struct { int8_t l, h; } sb;
    uint16_t w;
    int16_t sw;
};

template <typename T, typename U> constexpr T BIT(T x, U n) { return (x >> n) & T(1); }

enum { CLEAR_LINE = 0, ASSERT_LINE = 1 };
enum { INPUT_LINE_NMI = 32 };               // as MAME's; the 6809's own inputs are 0 (IRQ) and 1 (FIRQ)

#if defined(__GNUC__)
#define UNEXPECTED(x) __builtin_expect(!!(x), 0)
#define ATTR_FORCE_INLINE __attribute__((always_inline))
#else
#define UNEXPECTED(x) (x)
#define ATTR_FORCE_INLINE
#endif

#define LOGMASKED(...) ((void)0)
#define logerror(...) ((void)0)
#define debugger_exception_hook(n) ((void)0)
#define debugger_instruction_hook(a) ((void)0)
#define fatalerror(...) drv_fatal()
#undef assert
#define assert(x) ((void)((x) || (drv_fatal(), false)))

// ---- the framework's stand-ins ----------------------------------------------------------------------------------
struct devcb_write_line {                   // m_lic_func, m_syncack_write_func: unconnected
    void operator()(int) const {}
};

struct devcb_read8 {                        // m_vector_read_func: unset, so read_vector reads memory
    bool isunset() const { return true; }
    uint8_t operator()(uint32_t) const { return 0xff; }
};

struct m6809_memory {                       // m_mintf: MAME's memory_interface on the cpu_bus
    const cpu_bus *bus;
    uint8_t read(uint16_t a) const { return bus->read(bus->ctx, a); }
    uint8_t read_opcode(uint16_t a) const { return bus->fetch ? bus->fetch(bus->ctx, a) : bus->read(bus->ctx, a); }
    uint8_t read_opcode_arg(uint16_t a) const { return bus->read(bus->ctx, a); }
    void write(uint16_t a, uint8_t v) const { bus->write(bus->ctx, a, v); }
};

// The base of MAME's class here, in place of cpu_device: the bus and the step driver's state.
struct m6809_shim {
    m6809_memory m_space;
    m6809_memory *m_mintf;

    // the step driver's (m6809_mame.cpp)
    bool m_drv_sample;                      // this pass is a step's acceptance (A): MAIN looks at the lines
    bool m_drv_waiting;                     // the pass ended in a wait slot (eat_remaining: SYNC, CWAI)
    int m_drv_left;                         // the cycles the pass had left when it did
    bool m_drv_fault;                       // upstream's fatalerror or assert
    uint32_t m_drv_undefined;               // opcodes Motorola's Table 9 leaves unused, run since reset
    uint16_t m_drv_undefined_at;            // the last one's instruction address

    explicit m6809_shim(const cpu_bus *bus)
        : m_space{bus}, m_mintf(&m_space), m_drv_sample(false), m_drv_waiting(false), m_drv_left(0),
          m_drv_fault(false), m_drv_undefined(0), m_drv_undefined_at(0) {}

    void standard_irq_callback(int, uint16_t) {}
    void drv_fatal() { m_drv_fault = true; }
};

#endif
