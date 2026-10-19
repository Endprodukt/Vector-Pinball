#!/usr/bin/env python3
"""
vecdump.py - laesst ein Vectrex-ROM in einem kleinen 6809-Emulator laufen und schreibt mit, welche Striche es zeichnet.

Zweck: die Karte von Spinball (GCE 1983) exakt aus dem ROM holen, statt sie nach einem Foto nachzubauen.

Der Emulator rechnet die CPU vollstaendig, die Hardware nur so weit, wie es fuer die Geometrie noetig ist:
  - VIA 6522: Port A ist der DAC (X direkt, Y / Nullpunkt / Helligkeit ueber den Multiplexer von Port B),
    Timer 1 gibt die Dauer einer Rampe (der "scale"), das Schieberegister schaltet den Strahl an,
    CB2 (PCR) setzt den Strahl auf die Mitte zurueck.
  - Zeit wird nicht gezaehlt: jedes Warten auf einen Timer endet sofort. Ein Strich entsteht in dem Moment, in dem
    das Programm Timer 1 startet: Weg = (DAC X, gehaltenes Y) * (Zaehlerstand + 1,5).
  - Knoepfe ueber den Klangchip (Register 14), per Drehbuch drueckbar.

Aufruf:  python tools/vecdump.py ROM BIOS --frames 900 --press 4:300-310 --out strokes.json --png bild.png
"""
import argparse, json, sys

# ---------------------------------------------------------------- 6809
class CPU:
    def __init__(self, rd, wr):
        self.rd, self.wr = rd, wr
        self.a = self.b = self.dp = self.cc = 0
        self.x = self.y = self.u = self.s = self.pc = 0

    # --- Speicher
    def r16(self, a): return (self.rd(a) << 8) | self.rd((a + 1) & 0xFFFF)
    def w16(self, a, v): self.wr(a, (v >> 8) & 0xFF); self.wr((a + 1) & 0xFFFF, v & 0xFF)
    def f8(self):
        v = self.rd(self.pc); self.pc = (self.pc + 1) & 0xFFFF; return v
    def f16(self):
        v = self.r16(self.pc); self.pc = (self.pc + 2) & 0xFFFF; return v
    @property
    def d(self): return (self.a << 8) | self.b
    @d.setter
    def d(self, v): self.a = (v >> 8) & 0xFF; self.b = v & 0xFF

    # --- Flags: E F H I N Z V C = 80 40 20 10 08 04 02 01
    def nz8(self, v):
        self.cc = (self.cc & ~0x0C) | (0x08 if v & 0x80 else 0) | (0x04 if (v & 0xFF) == 0 else 0)
    def nz16(self, v):
        self.cc = (self.cc & ~0x0C) | (0x08 if v & 0x8000 else 0) | (0x04 if (v & 0xFFFF) == 0 else 0)
    def setc(self, c): self.cc = (self.cc & ~1) | (1 if c else 0)
    def setv(self, v): self.cc = (self.cc & ~2) | (2 if v else 0)

    def add8(self, a, b, c=0):
        r = a + b + c
        self.cc &= ~0x2F
        if (a ^ b ^ r) & 0x10: self.cc |= 0x20
        if r & 0x100: self.cc |= 1
        if (a ^ r) & (b ^ r) & 0x80: self.cc |= 2
        r &= 0xFF
        self.cc |= (0x08 if r & 0x80 else 0) | (0x04 if r == 0 else 0)
        return r
    def sub8(self, a, b, c=0):
        r = a - b - c
        self.cc &= ~0x0F
        if r & 0x100: self.cc |= 1
        if (a ^ b) & (a ^ r) & 0x80: self.cc |= 2
        r &= 0xFF
        self.cc |= (0x08 if r & 0x80 else 0) | (0x04 if r == 0 else 0)
        return r
    def add16(self, a, b):
        r = a + b
        self.cc &= ~0x0F
        if r & 0x10000: self.cc |= 1
        if (a ^ r) & (b ^ r) & 0x8000: self.cc |= 2
        r &= 0xFFFF
        self.cc |= (0x08 if r & 0x8000 else 0) | (0x04 if r == 0 else 0)
        return r
    def sub16(self, a, b):
        r = a - b
        self.cc &= ~0x0F
        if r & 0x10000: self.cc |= 1
        if (a ^ b) & (a ^ r) & 0x8000: self.cc |= 2
        r &= 0xFFFF
        self.cc |= (0x08 if r & 0x8000 else 0) | (0x04 if r == 0 else 0)
        return r

    # --- Adressierung
    def ea_direct(self): return (self.dp << 8) | self.f8()
    def ea_ext(self): return self.f16()
    def ea_idx(self):
        pb = self.f8()
        rn = (pb >> 5) & 3
        reg = ('x', 'y', 'u', 's')[rn]
        base = getattr(self, reg)
        if not pb & 0x80:
            off = pb & 0x1F
            if off & 0x10: off -= 0x20
            return (base + off) & 0xFFFF
        ind = pb & 0x10
        m = pb & 0x0F
        s8 = lambda v: v - 256 if v & 0x80 else v
        s16 = lambda v: v - 65536 if v & 0x8000 else v
        if m == 0: ea = base; setattr(self, reg, (base + 1) & 0xFFFF)
        elif m == 1: ea = base; setattr(self, reg, (base + 2) & 0xFFFF)
        elif m == 2: ea = (base - 1) & 0xFFFF; setattr(self, reg, ea)
        elif m == 3: ea = (base - 2) & 0xFFFF; setattr(self, reg, ea)
        elif m == 4: ea = base
        elif m == 5: ea = (base + s8(self.b)) & 0xFFFF
        elif m == 6: ea = (base + s8(self.a)) & 0xFFFF
        elif m == 8: ea = (base + s8(self.f8())) & 0xFFFF
        elif m == 9: ea = (base + s16(self.f16())) & 0xFFFF
        elif m == 11: ea = (base + self.d) & 0xFFFF
        elif m == 12: o = s8(self.f8()); ea = (self.pc + o) & 0xFFFF
        elif m == 13: o = s16(self.f16()); ea = (self.pc + o) & 0xFFFF
        elif m == 15: ea = self.f16()
        else: raise RuntimeError('idx mode %02X at %04X' % (pb, self.pc))
        if ind: ea = self.r16(ea)
        return ea

    # --- Stapel
    def push8(self, sp, v):
        setattr(self, sp, (getattr(self, sp) - 1) & 0xFFFF); self.wr(getattr(self, sp), v & 0xFF)
    def push16(self, sp, v): self.push8(sp, v & 0xFF); self.push8(sp, v >> 8)
    def pull8(self, sp):
        v = self.rd(getattr(self, sp)); setattr(self, sp, (getattr(self, sp) + 1) & 0xFFFF); return v
    def pull16(self, sp):
        h = self.pull8(sp); return (h << 8) | self.pull8(sp)
    def psh(self, sp, pb):
        other = 'u' if sp == 's' else 's'
        if pb & 0x80: self.push16(sp, self.pc)
        if pb & 0x40: self.push16(sp, getattr(self, other))
        if pb & 0x20: self.push16(sp, self.y)
        if pb & 0x10: self.push16(sp, self.x)
        if pb & 0x08: self.push8(sp, self.dp)
        if pb & 0x04: self.push8(sp, self.b)
        if pb & 0x02: self.push8(sp, self.a)
        if pb & 0x01: self.push8(sp, self.cc)
    def pul(self, sp, pb):
        other = 'u' if sp == 's' else 's'
        if pb & 0x01: self.cc = self.pull8(sp)
        if pb & 0x02: self.a = self.pull8(sp)
        if pb & 0x04: self.b = self.pull8(sp)
        if pb & 0x08: self.dp = self.pull8(sp)
        if pb & 0x10: self.x = self.pull16(sp)
        if pb & 0x20: self.y = self.pull16(sp)
        if pb & 0x40: setattr(self, other, self.pull16(sp))
        if pb & 0x80: self.pc = self.pull16(sp)

    REGS = {0: 'd', 1: 'x', 2: 'y', 3: 'u', 4: 's', 5: 'pc', 8: 'a', 9: 'b', 10: 'cc', 11: 'dp'}
    def getr(self, n): return getattr(self, self.REGS[n]) if n in self.REGS else 0xFF
    def setr(self, n, v):
        if n in self.REGS: setattr(self, self.REGS[n], v & (0xFFFF if n < 8 else 0xFF))

    # --- Ein-Operanden-Befehle (Speicher oder Akku)
    def op1(self, k, v):
        if k == 0x0:   r = self.sub8(0, v)                                     # NEG
        elif k == 0x3: r = (~v) & 0xFF; self.nz8(r); self.setv(0); self.setc(1)  # COM
        elif k == 0x4: self.setc(v & 1); r = v >> 1; self.nz8(r)                # LSR
        elif k == 0x6: c = self.cc & 1; self.setc(v & 1); r = (v >> 1) | (c << 7); self.nz8(r)   # ROR
        elif k == 0x7: self.setc(v & 1); r = (v >> 1) | (v & 0x80); self.nz8(r)  # ASR
        elif k == 0x8: self.setc(v & 0x80); r = (v << 1) & 0xFF; self.nz8(r); self.setv((v ^ r) & 0x80)   # ASL
        elif k == 0x9: c = self.cc & 1; self.setc(v & 0x80); r = ((v << 1) | c) & 0xFF; self.nz8(r); self.setv((v ^ r) & 0x80)  # ROL
        elif k == 0xA: r = (v - 1) & 0xFF; self.nz8(r); self.setv(v == 0x80)     # DEC
        elif k == 0xC: r = (v + 1) & 0xFF; self.nz8(r); self.setv(v == 0x7F)     # INC
        elif k == 0xD: r = None; self.nz8(v); self.setv(0)                       # TST
        elif k == 0xF: r = 0; self.cc = (self.cc & 0xF0) | 0x04                  # CLR
        else: raise RuntimeError('op1 %X at %04X' % (k, self.pc))
        return r

    def cond(self, n):
        c, v, z, ng = self.cc & 1, (self.cc >> 1) & 1, (self.cc >> 2) & 1, (self.cc >> 3) & 1
        return [True, False, not (c or z), bool(c or z), not c, bool(c), not z, bool(z), not v, bool(v),
                not ng, bool(ng), ng == v, ng != v, not (z or ng != v), bool(z or ng != v)][n]

    def step(self):
        op = self.f8()
        hi, lo = op >> 4, op & 15
        s8 = lambda v: v - 256 if v & 0x80 else v
        if op == 0x10 or op == 0x11:
            return self.step_page(op, self.f8())
        if hi in (0x0, 0x6, 0x7):
            ea = self.ea_direct() if hi == 0 else (self.ea_idx() if hi == 6 else self.ea_ext())
            if lo == 0xE: self.pc = ea; return
            r = self.op1(lo, self.rd(ea)) if lo != 0xF else self.op1(lo, 0)
            if r is not None: self.wr(ea, r)
            return
        if hi == 0x4:
            r = self.op1(lo, self.a)
            if r is not None: self.a = r
            return
        if hi == 0x5:
            r = self.op1(lo, self.b)
            if r is not None: self.b = r
            return
        if hi == 0x1:
            if op == 0x12: return
            if op == 0x13: return
            if op == 0x16: o = self.f16(); self.pc = (self.pc + o) & 0xFFFF; return
            if op == 0x17: o = self.f16(); self.push16('s', self.pc); self.pc = (self.pc + o) & 0xFFFF; return
            if op == 0x19:
                cf = 0; a = self.a
                if (a & 0x0F) > 9 or self.cc & 0x20: cf |= 0x06
                if (a >> 4) > 9 or self.cc & 1 or ((a >> 4) > 8 and (a & 0x0F) > 9): cf |= 0x60
                r = a + cf
                if r & 0x100: self.cc |= 1
                self.a = r & 0xFF; self.nz8(self.a); return
            if op == 0x1A: self.cc |= self.f8(); return
            if op == 0x1C: self.cc &= self.f8(); return
            if op == 0x1D: self.a = 0xFF if self.b & 0x80 else 0; self.nz16(self.d); return
            if op == 0x1E:
                pb = self.f8(); a, b = pb >> 4, pb & 15
                va, vb = self.getr(a), self.getr(b)
                self.setr(a, vb); self.setr(b, va); return
            if op == 0x1F:
                pb = self.f8(); self.setr(pb & 15, self.getr(pb >> 4)); return
        if hi == 0x2:
            o = s8(self.f8())
            if self.cond(lo): self.pc = (self.pc + o) & 0xFFFF
            return
        if hi == 0x3:
            if op == 0x30: self.x = self.ea_idx(); self.cc = (self.cc & ~4) | (4 if self.x == 0 else 0); return
            if op == 0x31: self.y = self.ea_idx(); self.cc = (self.cc & ~4) | (4 if self.y == 0 else 0); return
            if op == 0x32: self.s = self.ea_idx(); return
            if op == 0x33: self.u = self.ea_idx(); return
            if op == 0x34: self.psh('s', self.f8()); return
            if op == 0x35: self.pul('s', self.f8()); return
            if op == 0x36: self.psh('u', self.f8()); return
            if op == 0x37: self.pul('u', self.f8()); return
            if op == 0x39: self.pc = self.pull16('s'); return
            if op == 0x3A: self.x = (self.x + self.b) & 0xFFFF; return
            if op == 0x3B:
                self.cc = self.pull8('s')
                if self.cc & 0x80: self.pul('s', 0x7E)
                self.pc = self.pull16('s'); return
            if op == 0x3D:
                r = self.a * self.b; self.d = r
                self.cc = (self.cc & ~5) | (4 if r == 0 else 0) | (1 if r & 0x80 else 0); return
        if hi >= 0x8:
            mode = (hi - 8) & 3                      # 0 imm, 1 direct, 2 idx, 3 ext
            isb = hi >= 0xC
            wide = lo in (0x3, 0xC, 0xE) or (lo in (0xD, 0xF) and False)
            # Sonderfaelle zuerst
            if not isb and lo == 0xD:                # BSR / JSR
                if mode == 0: o = s8(self.f8()); self.push16('s', self.pc); self.pc = (self.pc + o) & 0xFFFF
                else:
                    ea = (self.ea_direct, self.ea_idx, self.ea_ext)[mode - 1]()
                    self.push16('s', self.pc); self.pc = ea
                return
            store = lo == 0x7 or (lo == 0xF) or (isb and lo == 0xD)
            if store:
                if mode == 0: raise RuntimeError('store imm %02X at %04X' % (op, self.pc))
                ea = (self.ea_direct, self.ea_idx, self.ea_ext)[mode - 1]()
                if lo == 0x7:
                    v = self.b if isb else self.a; self.wr(ea, v); self.nz8(v); self.setv(0)
                elif lo == 0xF:
                    v = self.u if isb else self.x; self.w16(ea, v); self.nz16(v); self.setv(0)
                else:
                    v = self.d; self.w16(ea, v); self.nz16(v); self.setv(0)
                return
            wide = lo in (0x3, 0xC, 0xE)
            if mode == 0: val = self.f16() if wide else self.f8()
            else:
                ea = (self.ea_direct, self.ea_idx, self.ea_ext)[mode - 1]()
                val = self.r16(ea) if wide else self.rd(ea)
            acc = self.b if isb else self.a
            def seta(v):
                if isb: self.b = v
                else: self.a = v
            if lo == 0x0: seta(self.sub8(acc, val))
            elif lo == 0x1: self.sub8(acc, val)
            elif lo == 0x2: seta(self.sub8(acc, val, self.cc & 1))
            elif lo == 0x3:
                if isb: self.d = self.add16(self.d, val)
                else: self.d = self.sub16(self.d, val)
            elif lo == 0x4: r = acc & val; seta(r); self.nz8(r); self.setv(0)
            elif lo == 0x5: self.nz8(acc & val); self.setv(0)
            elif lo == 0x6: seta(val); self.nz8(val); self.setv(0)
            elif lo == 0x8: r = acc ^ val; seta(r); self.nz8(r); self.setv(0)
            elif lo == 0x9: seta(self.add8(acc, val, self.cc & 1))
            elif lo == 0xA: r = acc | val; seta(r); self.nz8(r); self.setv(0)
            elif lo == 0xB: seta(self.add8(acc, val))
            elif lo == 0xC:
                if isb: self.d = val; self.nz16(val); self.setv(0)
                else: self.sub16(self.x, val)
            elif lo == 0xE:
                if isb: self.u = val
                else: self.x = val
                self.nz16(val); self.setv(0)
            else: raise RuntimeError('op %02X at %04X' % (op, self.pc))
            return
        raise RuntimeError('op %02X at %04X' % (op, (self.pc - 1) & 0xFFFF))

    def step_page(self, page, op):
        hi, lo = op >> 4, op & 15
        s16 = lambda v: v - 65536 if v & 0x8000 else v
        if page == 0x10 and hi == 2:
            o = self.f16()
            if self.cond(lo): self.pc = (self.pc + o) & 0xFFFF
            return
        if op == 0x3F: raise RuntimeError('SWI2/3 at %04X' % self.pc)
        mode = (hi - 8) & 3
        def operand16():
            if mode == 0: return self.f16()
            return self.r16((self.ea_direct, self.ea_idx, self.ea_ext)[mode - 1]())
        def ea():
            return (self.ea_direct, self.ea_idx, self.ea_ext)[mode - 1]()
        if page == 0x10:
            if hi in (8, 9, 0xA, 0xB):
                if lo == 0x3: self.sub16(self.d, operand16()); return
                if lo == 0xC: self.sub16(self.y, operand16()); return
                if lo == 0xE: self.y = operand16(); self.nz16(self.y); self.setv(0); return
                if lo == 0xF: self.w16(ea(), self.y); self.nz16(self.y); self.setv(0); return
            else:
                if lo == 0xE: self.s = operand16(); self.nz16(self.s); self.setv(0); return
                if lo == 0xF: self.w16(ea(), self.s); self.nz16(self.s); self.setv(0); return
        else:
            if lo == 0x3: self.sub16(self.u, operand16()); return
            if lo == 0xC: self.sub16(self.s, operand16()); return
        raise RuntimeError('op %02X %02X at %04X' % (page, op, self.pc))


# ---------------------------------------------------------------- Vectrex
class Vectrex:
    def __init__(self, rom, bios, script):
        self.rom = rom + bytes(0x8000 - len(rom))
        self.bios = bios
        self.ram = bytearray(0x400)
        self.via = [0] * 16
        self.psg = [0] * 16
        self.psg_addr = 0
        self.x = self.y = 0.0                # Strahl
        self.yhold = 0
        self.z = 0
        self.frame = 0
        self.strokes = []                    # (x0, y0, x1, y1, an, helligkeit, muster)
        self.frames = []
        self.script = script                 # {knopf: [(von, bis), ...]}
        self.sync = 0xF192                   # Bildgrenze: Wait_Recal oder eigene Hauptschleife des Spiels
        self.cpu = CPU(self.rd, self.wr)
        self.cpu.pc = (bios[0x1FFE] << 8) | bios[0x1FFF]

    def buttons(self):
        v = 0xFF
        for btn, spans in self.script.items():
            for a, b in spans:
                if a <= self.frame <= b: v &= ~(1 << (btn - 1))
        return v

    def rd(self, a):
        if a < 0x8000: return self.rom[a]
        if a >= 0xE000: return self.bios[a - 0xE000]
        if 0xC800 <= a < 0xD000: return self.ram[a & 0x3FF]
        if 0xD000 <= a < 0xD800:
            r = a & 15
            if r == 0x0: return (self.via[0] & 0xDF) | 0x00        # Vergleicher: Stick in der Mitte
            if r == 0x1 or r == 0xF:
                if (self.via[0] & 0x18) == 0x08:                   # BC1: Klangchip lesen
                    return self.buttons() if self.psg_addr == 14 else self.psg[self.psg_addr & 15]
                return self.via[1]
            if r == 0xD: return 0xFF                               # alle Timer abgelaufen: nie warten
            return self.via[r]
        return 0xFF

    def wr(self, a, v):
        if 0xC800 <= a < 0xD000: self.ram[a & 0x3FF] = v; return
        if not (0xD000 <= a < 0xD800): return
        r = a & 15
        s8 = lambda q: q - 256 if q & 0x80 else q
        if r == 0x0:
            self.via[0] = v
            if (v & 0x18) == 0x18: self.psg_addr = self.via[1] & 15
            elif (v & 0x18) == 0x10: self.psg[self.psg_addr] = self.via[1]
            self.mux()
        elif r == 0x1 or r == 0xF:
            self.via[1] = v
            self.mux()
        elif r == 0x5:                                              # Timer 1 starten = Rampe
            self.via[5] = v
            t = self.via[4] + (v << 8) + 1.5
            nx, ny = self.x + s8(self.via[1]) * t, self.y + self.yhold * t
            sr = self.via[0xA]
            self.strokes.append((self.x, self.y, nx, ny, 1 if sr else 0, self.z, sr))
            self.x, self.y = nx, ny
        elif r == 0x6:                                              # T1-Latch (tiefes Byte) = Zaehler beim Start
            self.via[6] = self.via[4] = v
        elif r == 0xC:
            self.via[0xC] = v
            if (v & 0xE0) == 0xC0: self.x = self.y = 0.0            # CB2 tief: Strahl auf die Mitte
        else:
            self.via[r] = v

    def mux(self):
        b = self.via[0]
        if b & 1: return
        sel = (b >> 1) & 3
        a = self.via[1]
        if sel == 0: self.yhold = a - 256 if a & 0x80 else a
        elif sel == 2: self.z = a

    def run(self, frames, max_steps=400_000_000):
        cpu = self.cpu
        for _ in range(max_steps):
            if cpu.pc == self.sync:                                 # Bildgrenze
                self.frames.append(self.strokes); self.strokes = []
                self.frame += 1
                if self.frame >= frames: return
            cpu.step()
        raise RuntimeError('zu viele Schritte (Bild %d, PC %04X)' % (self.frame, cpu.pc))


def render(strokes, path, size=900):
    from PIL import Image, ImageDraw
    lit = [s for s in strokes if s[4]]
    if not lit:
        print('nichts gezeichnet'); return
    xs = [v for s in lit for v in (s[0], s[2])]; ys = [v for s in lit for v in (s[1], s[3])]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    k = (size - 40) / max(x1 - x0, y1 - y0, 1)
    im = Image.new('RGB', (int((x1 - x0) * k) + 40, int((y1 - y0) * k) + 40))
    dr = ImageDraw.Draw(im)
    for i, s in enumerate(lit):
        p = (20 + (s[0] - x0) * k, 20 + (y1 - s[1]) * k, 20 + (s[2] - x0) * k, 20 + (y1 - s[3]) * k)
        dr.line(p, fill=(90, 255, 160) if s[6] == 0xFF else (255, 200, 60), width=1)
    im.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom'); ap.add_argument('bios')
    ap.add_argument('--frames', type=int, default=600)
    ap.add_argument('--press', action='append', default=[], help='knopf:von-bis (Bilder)')
    ap.add_argument('--frame', type=int, default=-1, help='welches Bild ausgeben (Vorgabe: das letzte)')
    ap.add_argument('--sync', default='F192', help='Adresse der Bildgrenze (hex)')
    ap.add_argument('--ram', help='RAM-Abbild C800-CBFF am Ende schreiben')
    ap.add_argument('--out'); ap.add_argument('--png'); ap.add_argument('--stats', action='store_true')
    a = ap.parse_args()
    script = {}
    for p in a.press:
        btn, span = p.split(':'); lo, hi = span.split('-')
        script.setdefault(int(btn), []).append((int(lo), int(hi)))
    v = Vectrex(open(a.rom, 'rb').read(), open(a.bios, 'rb').read(), script)
    v.sync = int(a.sync, 16)
    v.run(a.frames)
    if a.ram: open(a.ram, 'wb').write(bytes(v.ram))
    if a.stats:
        last = None
        for i, f in enumerate(v.frames):
            n = sum(1 for s in f if s[4])
            if n != last: print('Bild %4d: %3d Striche' % (i, n)); last = n
    f = v.frames[a.frame]
    if a.out: json.dump([list(s) for s in f], open(a.out, 'w'))
    if a.png: render(f, a.png)


if __name__ == '__main__':
    main()
