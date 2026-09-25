"""An Intel 8085 in plain Python: the documented instruction set, T-state counts, and the
8085's own interrupt system (TRAP, RST 7.5 / 6.5 / 5.5, RIM / SIM, SID / SOD).

Written for the Aicom Accent SA's program ROM (hosts/accent_sa.py).  The undocumented 8085
opcodes (DSUB, ARHL, RDEL, RSTV, LDHI, LDSI, SHLX, LHLX, JNK, JK) are not implemented: they
raise, so a ROM that needs one says so instead of running on wrong.

The host supplies read(addr), write(addr, value), port_in(port) and port_out(port, value).
"""

S, Z, AC, P, CY = 0x80, 0x40, 0x10, 0x04, 0x01
PARITY = [0] * 256
for _i in range(256):
    PARITY[_i] = P if bin(_i).count("1") % 2 == 0 else 0
SZP = [(_i & S) | (Z if _i == 0 else 0) | PARITY[_i] for _i in range(256)]

# T-states per opcode (the taken branch for conditionals is added in the code)
CYCLES = [
    4, 10, 7, 6, 4, 4, 7, 4, 0, 10, 7, 6, 4, 4, 7, 4,
    0, 10, 7, 6, 4, 4, 7, 4, 0, 10, 7, 6, 4, 4, 7, 4,
    4, 10, 16, 6, 4, 4, 7, 4, 0, 10, 16, 6, 4, 4, 7, 4,
    4, 10, 13, 6, 10, 10, 10, 4, 0, 10, 13, 6, 4, 4, 7, 4,
] + [4, 4, 4, 4, 4, 4, 7, 4] * 6 + [7, 7, 7, 7, 7, 7, 5, 7] + [4, 4, 4, 4, 4, 4, 7, 4] \
  + [4, 4, 4, 4, 4, 4, 7, 4] * 8 + [
    6, 10, 7, 10, 9, 12, 7, 12, 6, 10, 7, 0, 9, 18, 7, 12,
    6, 10, 7, 10, 9, 12, 7, 12, 6, 0, 7, 10, 9, 0, 7, 12,
    6, 10, 7, 16, 9, 12, 7, 12, 6, 6, 7, 4, 9, 0, 7, 12,
    6, 10, 7, 4, 9, 12, 7, 12, 6, 6, 7, 4, 9, 0, 7, 12,
]
assert len(CYCLES) == 256


class Unimplemented(Exception):
    pass


class I8085:
    def __init__(self, read, write, port_in, port_out):
        self.read, self.write = read, write
        self.port_in, self.port_out = port_in, port_out
        self.reset()

    def reset(self):
        self.a = self.b = self.c = self.d = self.e = self.h = self.l = 0
        self.f = 0x02
        self.sp = 0
        self.pc = 0
        self.ie = False
        self.ei_pending = False
        self.halted = False
        self.masks = 0x07            # M7.5 M6.5 M5.5 set at reset
        self.i75 = False             # RST 7.5 edge latch
        self.line55 = self.line65 = False
        self.trap_edge = False
        self.trap_ie = False         # IE as it was when TRAP was taken (RIM reports it)
        self.after_trap = False
        self.sid = 0
        self.sod = 0
        self.cycles = 0

    # ---- interrupt pins -------------------------------------------------------------------
    def trap(self):
        """A rising edge on TRAP."""
        self.trap_edge = True

    def rst75(self):
        """A rising edge on RST 7.5 (latched)."""
        self.i75 = True

    def set_rst55(self, level):
        self.line55 = bool(level)

    def set_rst65(self, level):
        self.line65 = bool(level)

    def _interrupt(self):
        if self.trap_edge:
            self.trap_edge = False
            self.trap_ie = self.ie
            self.after_trap = True
            self._take(0x24)
            return True
        if not self.ie:
            return False
        if self.i75 and not self.masks & 4:
            self.i75 = False
            self._take(0x3C)
            return True
        if self.line65 and not self.masks & 2:
            self._take(0x34)
            return True
        if self.line55 and not self.masks & 1:
            self._take(0x2C)
            return True
        return False

    def _take(self, vector):
        self.halted = False
        self.ie = False
        self.ei_pending = False
        self._push(self.pc)
        self.pc = vector
        self.cycles += 12

    # ---- helpers ------------------------------------------------------------------------
    def _push(self, v):
        self.sp = (self.sp - 1) & 0xFFFF
        self.write(self.sp, v >> 8)
        self.sp = (self.sp - 1) & 0xFFFF
        self.write(self.sp, v & 0xFF)

    def _pop(self):
        lo = self.read(self.sp)
        hi = self.read((self.sp + 1) & 0xFFFF)
        self.sp = (self.sp + 2) & 0xFFFF
        return (hi << 8) | lo

    def _imm16(self):
        pc = self.pc
        v = self.read(pc) | (self.read((pc + 1) & 0xFFFF) << 8)
        self.pc = (pc + 2) & 0xFFFF
        return v

    def _get(self, r):
        if r == 0: return self.b
        if r == 1: return self.c
        if r == 2: return self.d
        if r == 3: return self.e
        if r == 4: return self.h
        if r == 5: return self.l
        if r == 6: return self.read((self.h << 8) | self.l)
        return self.a

    def _set(self, r, v):
        if r == 0: self.b = v
        elif r == 1: self.c = v
        elif r == 2: self.d = v
        elif r == 3: self.e = v
        elif r == 4: self.h = v
        elif r == 5: self.l = v
        elif r == 6: self.write((self.h << 8) | self.l, v)
        else: self.a = v

    def _alu(self, op, v):
        a = self.a
        if op == 0 or op == 1:                               # ADD ADC
            c = self.f & CY if op == 1 else 0
            r = a + v + c
            self.f = SZP[r & 0xFF] | (CY if r > 0xFF else 0) | (AC if ((a & 15) + (v & 15) + c) > 15 else 0) | 2
            self.a = r & 0xFF
        elif op == 2 or op == 3 or op == 7:                  # SUB SBB CMP
            c = self.f & CY if op == 3 else 0
            r = a - v - c
            self.f = SZP[r & 0xFF] | (CY if r < 0 else 0) | (AC if ((a & 15) - (v & 15) - c) >= 0 else 0) | 2
            if op != 7:
                self.a = r & 0xFF
        elif op == 4:                                        # ANA: the 8085 sets AC
            self.a = a & v
            self.f = SZP[self.a] | AC | 2
        elif op == 5:                                        # XRA
            self.a = a ^ v
            self.f = SZP[self.a] | 2
        else:                                                # ORA
            self.a = a | v
            self.f = SZP[self.a] | 2

    def _cond(self, cc):
        f = self.f
        if cc == 0: return not f & Z
        if cc == 1: return bool(f & Z)
        if cc == 2: return not f & CY
        if cc == 3: return bool(f & CY)
        if cc == 4: return not f & P
        if cc == 5: return bool(f & P)
        if cc == 6: return not f & S
        return bool(f & S)

    def _rp(self, rp):
        if rp == 0: return (self.b << 8) | self.c
        if rp == 1: return (self.d << 8) | self.e
        if rp == 2: return (self.h << 8) | self.l
        return self.sp

    def _set_rp(self, rp, v):
        v &= 0xFFFF
        if rp == 0: self.b, self.c = v >> 8, v & 0xFF
        elif rp == 1: self.d, self.e = v >> 8, v & 0xFF
        elif rp == 2: self.h, self.l = v >> 8, v & 0xFF
        else: self.sp = v

    # ---- execution ----------------------------------------------------------------------
    def run(self, cycles):
        """Run at least `cycles` T-states (or stay halted through them).  Returns the count run."""
        start = self.cycles
        end = start + cycles
        while self.cycles < end:
            if self.ei_pending:
                self.ei_pending = False
                self.ie = True
            elif self.trap_edge or self.ie:
                if self._interrupt():
                    continue
            if self.halted:
                self.cycles = end
                break
            self.step()
        return self.cycles - start

    def step(self):
        pc = self.pc
        op = self.read(pc)
        self.pc = (pc + 1) & 0xFFFF
        self.cycles += CYCLES[op]
        if 0x40 <= op < 0x80:
            if op == 0x76:
                self.halted = True
                return
            self._set((op >> 3) & 7, self._get(op & 7))
            return
        if 0x80 <= op < 0xC0:
            self._alu((op >> 3) & 7, self._get(op & 7))
            return
        if op < 0x40:
            lo = op & 7
            if lo == 6:                                      # MVI
                v = self.read(self.pc)
                self.pc = (self.pc + 1) & 0xFFFF
                self._set(op >> 3, v)
            elif lo == 4:                                    # INR
                r = op >> 3
                v = (self._get(r) + 1) & 0xFF
                self._set(r, v)
                self.f = (self.f & CY) | SZP[v] | (AC if (v & 15) == 0 else 0) | 2
            elif lo == 5:                                    # DCR
                r = op >> 3
                v = (self._get(r) - 1) & 0xFF
                self._set(r, v)
                self.f = (self.f & CY) | SZP[v] | (AC if (v & 15) != 15 else 0) | 2
            elif lo == 3:                                    # INX / DCX
                rp = (op >> 4) & 3
                self._set_rp(rp, self._rp(rp) + (-1 if op & 8 else 1))
            elif lo == 1:
                rp = (op >> 4) & 3
                if op & 8:                                   # DAD
                    r = self._rp(2) + self._rp(rp)
                    self._set_rp(2, r)
                    self.f = (self.f & ~CY & 0xFF) | (CY if r > 0xFFFF else 0)
                else:                                        # LXI
                    self._set_rp(rp, self._imm16())
            elif lo == 2:
                if op == 0x02: self.write((self.b << 8) | self.c, self.a)
                elif op == 0x12: self.write((self.d << 8) | self.e, self.a)
                elif op == 0x0A: self.a = self.read((self.b << 8) | self.c)
                elif op == 0x1A: self.a = self.read((self.d << 8) | self.e)
                elif op == 0x22:
                    ad = self._imm16()
                    self.write(ad, self.l)
                    self.write((ad + 1) & 0xFFFF, self.h)
                elif op == 0x2A:
                    ad = self._imm16()
                    self.l = self.read(ad)
                    self.h = self.read((ad + 1) & 0xFFFF)
                elif op == 0x32: self.write(self._imm16(), self.a)
                else: self.a = self.read(self._imm16())
            elif lo == 7:
                a = self.a
                if op == 0x07:                               # RLC
                    c = a >> 7
                    self.a = ((a << 1) | c) & 0xFF
                    self.f = (self.f & ~CY & 0xFF) | c
                elif op == 0x0F:                             # RRC
                    c = a & 1
                    self.a = (a >> 1) | (c << 7)
                    self.f = (self.f & ~CY & 0xFF) | c
                elif op == 0x17:                             # RAL
                    c = a >> 7
                    self.a = ((a << 1) | (self.f & CY)) & 0xFF
                    self.f = (self.f & ~CY & 0xFF) | c
                elif op == 0x1F:                             # RAR
                    c = a & 1
                    self.a = (a >> 1) | ((self.f & CY) << 7)
                    self.f = (self.f & ~CY & 0xFF) | c
                elif op == 0x27:                             # DAA
                    corr, c = 0, self.f & CY
                    if (a & 15) > 9 or self.f & AC:
                        corr = 6
                    if a > 0x99 or c:
                        corr |= 0x60
                        c = CY
                    r = a + corr
                    self.f = SZP[r & 0xFF] | c | (AC if ((a & 15) + (corr & 15)) > 15 else 0) | 2
                    self.a = r & 0xFF
                elif op == 0x2F: self.a = a ^ 0xFF          # CMA
                elif op == 0x37: self.f |= CY               # STC
                else: self.f ^= CY                          # CMC
            else:                                            # lo == 0
                if op == 0x00:
                    pass
                elif op == 0x20:                             # RIM
                    ie = self.trap_ie if self.after_trap else self.ie
                    self.after_trap = False
                    self.a = ((self.sid & 1) << 7) | (0x40 if self.i75 else 0) \
                        | (0x20 if self.line65 else 0) | (0x10 if self.line55 else 0) \
                        | (0x08 if ie else 0) | self.masks
                elif op == 0x30:                             # SIM
                    a = self.a
                    if a & 0x08:
                        self.masks = a & 7
                    if a & 0x10:
                        self.i75 = False
                    if a & 0x40:
                        self.sod = a >> 7
                else:
                    raise Unimplemented("undocumented 8085 opcode %02X at %04X" % (op, pc))
            return
        # 0xC0 - 0xFF
        lo = op & 7
        if lo == 0:                                          # Rcc
            if self._cond((op >> 3) & 7):
                self.pc = self._pop()
                self.cycles += 6
        elif lo == 2:                                        # Jcc
            ad = self._imm16()
            if self._cond((op >> 3) & 7):
                self.pc = ad
                self.cycles += 3
        elif lo == 4:                                        # Ccc
            ad = self._imm16()
            if self._cond((op >> 3) & 7):
                self._push(self.pc)
                self.pc = ad
                self.cycles += 9
        elif lo == 6:                                        # ALU immediate
            v = self.read(self.pc)
            self.pc = (self.pc + 1) & 0xFFFF
            self._alu((op >> 3) & 7, v)
        elif lo == 7:                                        # RST
            self._push(self.pc)
            self.pc = op & 0x38
        elif lo == 1:
            if op & 8:
                if op == 0xC9: self.pc = self._pop()
                elif op == 0xE9: self.pc = (self.h << 8) | self.l
                elif op == 0xF9: self.sp = (self.h << 8) | self.l
                else: raise Unimplemented("undocumented 8085 opcode %02X at %04X" % (op, pc))
            else:                                            # POP
                v = self._pop()
                rp = (op >> 4) & 3
                if rp == 3:
                    self.a, self.f = v >> 8, (v & 0xD5) | 2
                else:
                    self._set_rp(rp, v)
        elif lo == 5:
            if op & 8:
                if op == 0xCD:
                    ad = self._imm16()
                    self._push(self.pc)
                    self.pc = ad
                else:
                    raise Unimplemented("undocumented 8085 opcode %02X at %04X" % (op, pc))
            else:                                            # PUSH
                rp = (op >> 4) & 3
                self._push(((self.a << 8) | self.f) if rp == 3 else self._rp(rp))
        else:                                                # lo == 3
            if op == 0xC3:
                self.pc = self._imm16()
            elif op == 0xD3:
                p = self.read(self.pc)
                self.pc = (self.pc + 1) & 0xFFFF
                self.port_out(p, self.a)
            elif op == 0xDB:
                p = self.read(self.pc)
                self.pc = (self.pc + 1) & 0xFFFF
                self.a = self.port_in(p) & 0xFF
            elif op == 0xE3:                                 # XTHL
                v = self._pop()
                self._push((self.h << 8) | self.l)
                self.h, self.l = v >> 8, v & 0xFF
            elif op == 0xEB:                                 # XCHG
                self.d, self.h = self.h, self.d
                self.e, self.l = self.l, self.e
            elif op == 0xF3:
                self.ie = False
                self.ei_pending = False
            elif op == 0xFB:
                self.ei_pending = True
            else:
                raise Unimplemented("undocumented 8085 opcode %02X at %04X" % (op, pc))
