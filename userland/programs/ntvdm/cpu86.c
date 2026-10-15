/* cpu86.c — a real-mode x86 interpreter: the 8086's instruction set with
 * the 80186's additions (PUSHA, PUSH immediate, IMUL immediate, shifts by
 * an immediate count, ENTER/LEAVE, BOUND, INS/OUTS).  The flags behave as
 * an 8086's (bits 12 to 15 read as ones, so programs that look for a 286
 * see an 8086 and stay in real mode); there is no coprocessor (ESC
 * instructions do nothing, as on a PC without an 8087).  See cpu86.h. */
#include "cpu86.h"

#define AX c->r.w[CPU86_AX]
#define CX c->r.w[CPU86_CX]
#define DX c->r.w[CPU86_DX]
#define BX c->r.w[CPU86_BX]
#define SP c->r.w[CPU86_SP]
#define BP c->r.w[CPU86_BP]
#define SI c->r.w[CPU86_SI]
#define DI c->r.w[CPU86_DI]
#define AL c->r.b[0]
#define AH c->r.b[1]
#define CL c->r.b[2]
#define ES c->sreg[CPU86_ES]
#define CS c->sreg[CPU86_CS]
#define SS c->sreg[CPU86_SS]
#define DS c->sreg[CPU86_DS]

#define CF CPU86_CF
#define PF CPU86_PF
#define AF CPU86_AF
#define ZF CPU86_ZF
#define SF CPU86_SF
#define TF CPU86_TF
#define IF CPU86_IF
#define DF CPU86_DF
#define OF CPU86_OF

#define FLAGS_FIXED 0xF002          /* (an 8086: bits 12-15 and bit 1 always set) */

static uint8_t parity[256];

/* memory: the low 1 MiB wraps (A20 off); the BIOS ROM (F0000-FFFFF) is
 * read-only to programs */
uint8_t cpu86_rd8(Cpu86 *c, uint16_t seg, uint16_t off) { return c->mem[cpu86_lin(seg, off)]; }
uint16_t cpu86_rd16(Cpu86 *c, uint16_t seg, uint16_t off)
{
    return (uint16_t)(cpu86_rd8(c, seg, off) | cpu86_rd8(c, seg, (uint16_t)(off + 1)) << 8);
}
void cpu86_wr8(Cpu86 *c, uint16_t seg, uint16_t off, uint8_t v)
{
    uint32_t a = cpu86_lin(seg, off);
    if (a < 0xF0000) c->mem[a] = v;
}
void cpu86_wr16(Cpu86 *c, uint16_t seg, uint16_t off, uint16_t v)
{
    cpu86_wr8(c, seg, off, (uint8_t)v);
    cpu86_wr8(c, seg, (uint16_t)(off + 1), (uint8_t)(v >> 8));
}
void cpu86_push(Cpu86 *c, uint16_t v) { SP -= 2; cpu86_wr16(c, SS, SP, v); }
uint16_t cpu86_pop(Cpu86 *c) { uint16_t v = cpu86_rd16(c, SS, SP); SP += 2; return v; }

static uint8_t fetch8(Cpu86 *c) { return cpu86_rd8(c, CS, c->ip++); }
static uint16_t fetch16(Cpu86 *c) { uint16_t v = cpu86_rd16(c, CS, c->ip); c->ip += 2; return v; }

void cpu86_reset(Cpu86 *c)
{
    for (int i = 0; i < 256; i++) {
        int p = 1;
        for (int b = 0; b < 8; b++) if (i & (1 << b)) p ^= 1;
        parity[i] = (uint8_t)(p ? PF : 0);
    }
    for (int i = 0; i < 8; i++) c->r.w[i] = 0;
    ES = SS = DS = 0;
    CS = 0xF000;
    c->ip = 0xFFF0;
    c->flags = FLAGS_FIXED;
    c->halted = 0;
    c->inhibit = 0;
}

void cpu86_interrupt(Cpu86 *c, uint8_t v)
{
    cpu86_push(c, c->flags);
    cpu86_push(c, CS);
    cpu86_push(c, c->ip);
    c->flags &= (uint16_t)~(IF | TF);
    c->ip = cpu86_rd16(c, 0, (uint16_t)(v * 4));
    CS = cpu86_rd16(c, 0, (uint16_t)(v * 4 + 2));
}

/* ---- flags ---- */

static void szp(Cpu86 *c, uint32_t r, int w)
{
    uint32_t mask = w ? 0xFFFF : 0xFF;
    c->flags &= (uint16_t)~(ZF | SF | PF);
    if (!(r & mask)) c->flags |= ZF;
    if (r & (w ? 0x8000 : 0x80)) c->flags |= SF;
    c->flags |= parity[r & 0xFF];
}

static void setf(Cpu86 *c, uint16_t f, int on)
{
    if (on) c->flags |= f; else c->flags &= (uint16_t)~f;
}

/* ADD OR ADC SBB AND SUB XOR CMP (op 0-7) */
static uint16_t alu(Cpu86 *c, int op, uint16_t a, uint16_t b, int w)
{
    uint32_t mask = w ? 0xFFFF : 0xFF, sign = w ? 0x8000 : 0x80, r;
    uint32_t cin = (op == 2 || op == 3) && (c->flags & CF) ? 1 : 0;
    a &= mask; b &= mask;
    switch (op) {
    case 0: case 2:
        r = (uint32_t)a + b + cin;
        setf(c, CF, r > mask);
        setf(c, OF, ((a ^ r) & (b ^ r) & sign) != 0);
        setf(c, AF, ((a ^ b ^ r) & 0x10) != 0);
        break;
    case 3: case 5: case 7:
        r = (uint32_t)a - b - cin;
        setf(c, CF, (uint32_t)a < (uint32_t)b + cin);
        setf(c, OF, ((a ^ b) & (a ^ r) & sign) != 0);
        setf(c, AF, ((a ^ b ^ r) & 0x10) != 0);
        break;
    default:
        r = op == 1 ? (a | b) : op == 4 ? (a & b) : (a ^ b);
        c->flags &= (uint16_t)~(CF | OF | AF);
        break;
    }
    szp(c, r, w);
    return (uint16_t)(r & mask);
}

static uint16_t incdec(Cpu86 *c, uint16_t a, int dec, int w)
{
    uint16_t cf = c->flags & CF;
    uint16_t r = alu(c, dec ? 5 : 0, a, 1, w);
    c->flags = (uint16_t)((c->flags & ~CF) | cf);
    return r;
}

/* ROL ROR RCL RCR SHL SHR SAL SAR (op 0-7) */
static uint16_t shift(Cpu86 *c, int op, uint16_t v, uint8_t n, int w)
{
    uint32_t mask = w ? 0xFFFF : 0xFF, sign = w ? 0x8000 : 0x80;
    uint32_t x = v & mask;
    n &= 0x1F;
    if (!n) return (uint16_t)x;
    int cf = (c->flags & CF) != 0, of = 0;
    for (int i = 0; i < n; i++) {
        switch (op) {
        case 0: cf = (x & sign) != 0; x = ((x << 1) | (uint32_t)cf) & mask; break;
        case 1: cf = x & 1; x = (x >> 1) | (cf ? sign : 0); break;
        case 2: { int t = (x & sign) != 0; x = ((x << 1) | (uint32_t)cf) & mask; cf = t; break; }
        case 3: { int t = x & 1; x = (x >> 1) | (cf ? sign : 0); cf = t; break; }
        case 4: case 6: cf = (x & sign) != 0; x = (x << 1) & mask; break;
        case 5: cf = x & 1; x >>= 1; break;
        case 7: cf = x & 1; x = (x >> 1) | (x & sign); break;
        }
    }
    switch (op) {
    case 0: case 2: case 4: case 6: of = ((x & sign) != 0) ^ cf; break;
    case 1: case 3: of = ((x ^ (x << 1)) & sign) != 0; break;
    case 5: of = n == 1 ? ((v & sign) != 0) : 0; break;
    case 7: of = 0; break;
    }
    setf(c, CF, cf);
    setf(c, OF, of);
    if (op >= 4) szp(c, x, w);
    return (uint16_t)x;
}

/* ---- operands ---- */

static uint8_t *reg8(Cpu86 *c, int i) { return i < 4 ? &c->r.b[i * 2] : &c->r.b[(i - 4) * 2 + 1]; }

static void modrm(Cpu86 *c)
{
    uint8_t m = fetch8(c);
    c->mod = m >> 6;
    c->reg = (m >> 3) & 7;
    c->rm = m & 7;
    if (c->mod == 3) return;
    uint16_t off = 0;
    int seg = CPU86_DS;
    switch (c->rm) {
    case 0: off = BX + SI; break;
    case 1: off = BX + DI; break;
    case 2: off = BP + SI; seg = CPU86_SS; break;
    case 3: off = BP + DI; seg = CPU86_SS; break;
    case 4: off = SI; break;
    case 5: off = DI; break;
    case 6:
        if (c->mod == 0) off = fetch16(c);
        else { off = BP; seg = CPU86_SS; }
        break;
    case 7: off = BX; break;
    }
    if (c->mod == 1) off += (uint16_t)(int8_t)fetch8(c);
    else if (c->mod == 2) off += fetch16(c);
    c->ea_seg = c->sreg[c->seg_ovr >= 0 ? c->seg_ovr : seg];
    c->ea_off = off;
}

static uint16_t getE(Cpu86 *c, int w)
{
    if (c->mod == 3) return w ? c->r.w[c->rm] : *reg8(c, c->rm);
    return w ? cpu86_rd16(c, c->ea_seg, c->ea_off) : cpu86_rd8(c, c->ea_seg, c->ea_off);
}

static void setE(Cpu86 *c, int w, uint16_t v)
{
    if (c->mod == 3) {
        if (w) c->r.w[c->rm] = v; else *reg8(c, c->rm) = (uint8_t)v;
    } else if (w) cpu86_wr16(c, c->ea_seg, c->ea_off, v);
    else cpu86_wr8(c, c->ea_seg, c->ea_off, (uint8_t)v);
}

static uint16_t getG(Cpu86 *c, int w) { return w ? c->r.w[c->reg] : *reg8(c, c->reg); }
static void setG(Cpu86 *c, int w, uint16_t v)
{
    if (w) c->r.w[c->reg] = v; else *reg8(c, c->reg) = (uint8_t)v;
}

static uint16_t dseg(Cpu86 *c) { return c->sreg[c->seg_ovr >= 0 ? c->seg_ovr : CPU86_DS]; }

static int cond(Cpu86 *c, int cc)
{
    uint16_t f = c->flags;
    int r;
    switch (cc >> 1) {
    case 0: r = (f & OF) != 0; break;
    case 1: r = (f & CF) != 0; break;
    case 2: r = (f & ZF) != 0; break;
    case 3: r = (f & (CF | ZF)) != 0; break;
    case 4: r = (f & SF) != 0; break;
    case 5: r = (f & PF) != 0; break;
    case 6: r = ((f & SF) != 0) != ((f & OF) != 0); break;
    default: r = (f & ZF) || (((f & SF) != 0) != ((f & OF) != 0)); break;
    }
    return (cc & 1) ? !r : r;
}

static void divide_error(Cpu86 *c)
{
    cpu86_interrupt(c, 0);           /* (8086: returns after the DIV) */
}

/* MUL IMUL DIV IDIV and the rest of group 3 (F6/F7) */
static void group3(Cpu86 *c, int w)
{
    uint16_t v = getE(c, w);
    switch (c->reg) {
    case 0: case 1:
        alu(c, 4, v, w ? fetch16(c) : fetch8(c), w);
        break;
    case 2: setE(c, w, (uint16_t)~v); break;
    case 3: {
        uint16_t r = alu(c, 5, 0, v, w);
        setf(c, CF, (w ? v : (v & 0xFF)) != 0);
        setE(c, w, r);
        break;
    }
    case 4:
        if (w) {
            uint32_t r = (uint32_t)AX * v;
            AX = (uint16_t)r; DX = (uint16_t)(r >> 16);
            setf(c, CF | OF, DX != 0);
        } else {
            AX = (uint16_t)(AL * (uint8_t)v);
            setf(c, CF | OF, AH != 0);
        }
        break;
    case 5:
        if (w) {
            int32_t r = (int32_t)(int16_t)AX * (int16_t)v;
            AX = (uint16_t)r; DX = (uint16_t)((uint32_t)r >> 16);
            setf(c, CF | OF, r != (int16_t)r);
        } else {
            int16_t r = (int16_t)((int8_t)AL * (int8_t)v);
            AX = (uint16_t)r;
            setf(c, CF | OF, r != (int8_t)r);
        }
        break;
    case 6:
        if (w) {
            uint32_t n = ((uint32_t)DX << 16) | AX;
            if (!v || n / v > 0xFFFF) { divide_error(c); return; }
            AX = (uint16_t)(n / v); DX = (uint16_t)(n % v);
        } else {
            uint8_t d = (uint8_t)v;
            if (!d || AX / d > 0xFF) { divide_error(c); return; }
            uint16_t n = AX;
            AL = (uint8_t)(n / d); AH = (uint8_t)(n % d);
        }
        break;
    case 7:
        if (w) {
            int32_t n = (int32_t)(((uint32_t)DX << 16) | AX);
            int16_t d = (int16_t)v;
            if (!d || (n == INT32_MIN && d == -1)) { divide_error(c); return; }
            int32_t q = n / d;
            if (q > 32767 || q < -32768) { divide_error(c); return; }
            AX = (uint16_t)q; DX = (uint16_t)(n % d);
        } else {
            int16_t n = (int16_t)AX;
            int8_t d = (int8_t)v;
            if (!d) { divide_error(c); return; }
            int q = n / d;
            if (q > 127 || q < -128) { divide_error(c); return; }
            AL = (uint8_t)q; AH = (uint8_t)(n % d);
        }
        break;
    }
}

/* ---- string instructions ---- */

static void string_op(Cpu86 *c, uint8_t op)
{
    int w = op & 1;
    int16_t d = (int16_t)((c->flags & DF) ? -(1 + w) : (1 + w));
    uint16_t src = dseg(c);
    for (;;) {
        if (c->rep && !CX) break;
        switch (op) {
        case 0x6C: case 0x6D:             /* INS */
            if (w) cpu86_wr16(c, ES, DI, (uint16_t)(c->in8(c->machine, DX) | c->in8(c->machine, (uint16_t)(DX + 1)) << 8));
            else cpu86_wr8(c, ES, DI, c->in8(c->machine, DX));
            DI += (uint16_t)d;
            break;
        case 0x6E: case 0x6F: {           /* OUTS */
            uint16_t v = w ? cpu86_rd16(c, src, SI) : cpu86_rd8(c, src, SI);
            c->out8(c->machine, DX, (uint8_t)v);
            if (w) c->out8(c->machine, (uint16_t)(DX + 1), (uint8_t)(v >> 8));
            SI += (uint16_t)d;
            break;
        }
        case 0xA4: case 0xA5:             /* MOVS */
            if (w) cpu86_wr16(c, ES, DI, cpu86_rd16(c, src, SI));
            else cpu86_wr8(c, ES, DI, cpu86_rd8(c, src, SI));
            SI += (uint16_t)d; DI += (uint16_t)d;
            break;
        case 0xA6: case 0xA7:             /* CMPS */
            alu(c, 7, w ? cpu86_rd16(c, src, SI) : cpu86_rd8(c, src, SI),
                w ? cpu86_rd16(c, ES, DI) : cpu86_rd8(c, ES, DI), w);
            SI += (uint16_t)d; DI += (uint16_t)d;
            break;
        case 0xAA: case 0xAB:             /* STOS */
            if (w) cpu86_wr16(c, ES, DI, AX); else cpu86_wr8(c, ES, DI, AL);
            DI += (uint16_t)d;
            break;
        case 0xAC: case 0xAD:             /* LODS */
            if (w) AX = cpu86_rd16(c, src, SI); else AL = cpu86_rd8(c, src, SI);
            SI += (uint16_t)d;
            break;
        case 0xAE: case 0xAF:             /* SCAS */
            alu(c, 7, w ? AX : AL, w ? cpu86_rd16(c, ES, DI) : cpu86_rd8(c, ES, DI), w);
            DI += (uint16_t)d;
            break;
        }
        if (!c->rep) break;
        CX--;
        if ((op & 0xF6) == 0xA6 || (op & 0xFE) == 0xAE) {     /* CMPS/SCAS: REPE/REPNE */
            int z = (c->flags & ZF) != 0;
            if ((c->rep == 0xF3 && !z) || (c->rep == 0xF2 && z)) break;
        }
    }
}

/* ---- one instruction ---- */

static void step(Cpu86 *c)
{
    uint8_t op;
    c->seg_ovr = -1;
    c->rep = 0;
    c->start_ip = c->ip;
    for (;;) {
        op = fetch8(c);
        if (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E) c->seg_ovr = (op >> 3) & 3;
        else if (op == 0xF2 || op == 0xF3) c->rep = op;
        else if (op != 0xF0) break;
    }

    if (op < 0x40 && (op & 7) < 6) {                /* ADD OR ADC SBB AND SUB XOR CMP */
        int alu_op = op >> 3, w = op & 1;
        switch (op & 7) {
        case 0: case 1: {
            modrm(c);
            uint16_t r = alu(c, alu_op, getE(c, w), getG(c, w), w);
            if (alu_op != 7) setE(c, w, r);
            break;
        }
        case 2: case 3: {
            modrm(c);
            uint16_t r = alu(c, alu_op, getG(c, w), getE(c, w), w);
            if (alu_op != 7) setG(c, w, r);
            break;
        }
        case 4: { uint8_t r = (uint8_t)alu(c, alu_op, AL, fetch8(c), 0); if (alu_op != 7) AL = r; break; }
        case 5: { uint16_t r = alu(c, alu_op, AX, fetch16(c), 1); if (alu_op != 7) AX = r; break; }
        }
        return;
    }

    switch (op) {
    case 0x06: case 0x0E: case 0x16: case 0x1E: cpu86_push(c, c->sreg[op >> 3]); break;
    case 0x07: case 0x17: case 0x1F:
        c->sreg[op >> 3] = cpu86_pop(c);
        if (op == 0x17) c->inhibit = 1;
        break;
    case 0x27: {                                     /* DAA */
        uint8_t old = AL; int cf = (c->flags & CF) != 0;
        if ((AL & 0x0F) > 9 || (c->flags & AF)) { AL += 6; c->flags |= AF; } else c->flags &= (uint16_t)~AF;
        if (old > 0x99 || cf) { AL += 0x60; c->flags |= CF; } else c->flags &= (uint16_t)~CF;
        szp(c, AL, 0);
        break;
    }
    case 0x2F: {                                     /* DAS */
        uint8_t old = AL; int cf = (c->flags & CF) != 0;
        if ((AL & 0x0F) > 9 || (c->flags & AF)) { AL -= 6; c->flags |= AF; } else c->flags &= (uint16_t)~AF;
        if (old > 0x99 || cf) { AL -= 0x60; c->flags |= CF; } else c->flags &= (uint16_t)~CF;
        szp(c, AL, 0);
        break;
    }
    case 0x37: case 0x3F:                            /* AAA AAS */
        if ((AL & 0x0F) > 9 || (c->flags & AF)) {
            if (op == 0x37) { AX += 0x106; } else { AL -= 6; AH -= 1; }
            c->flags |= AF | CF;
        } else c->flags &= (uint16_t)~(AF | CF);
        AL &= 0x0F;
        break;
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        c->r.w[op & 7] = incdec(c, c->r.w[op & 7], 0, 1); break;
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        c->r.w[op & 7] = incdec(c, c->r.w[op & 7], 1, 1); break;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x55: case 0x56: case 0x57:
        cpu86_push(c, c->r.w[op & 7]); break;
    case 0x54: { uint16_t v = SP; cpu86_push(c, (uint16_t)(v - 2)); break; }    /* (8086: the new SP) */
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: {
        uint16_t v = cpu86_pop(c);
        c->r.w[op & 7] = v;
        break;
    }
    case 0x60: {                                     /* PUSHA */
        uint16_t sp = SP;
        for (int i = 0; i < 8; i++) cpu86_push(c, i == CPU86_SP ? sp : c->r.w[i]);
        break;
    }
    case 0x61:                                       /* POPA */
        for (int i = 7; i >= 0; i--) { uint16_t v = cpu86_pop(c); if (i != CPU86_SP) c->r.w[i] = v; }
        break;
    case 0x62: {                                     /* BOUND */
        modrm(c);
        int16_t v = (int16_t)getG(c, 1);
        int16_t lo = (int16_t)cpu86_rd16(c, c->ea_seg, c->ea_off);
        int16_t hi = (int16_t)cpu86_rd16(c, c->ea_seg, (uint16_t)(c->ea_off + 2));
        if (v < lo || v > hi) { c->ip = c->start_ip; cpu86_interrupt(c, 5); }
        break;
    }
    case 0x68: cpu86_push(c, fetch16(c)); break;
    case 0x6A: cpu86_push(c, (uint16_t)(int8_t)fetch8(c)); break;
    case 0x69: case 0x6B: {                          /* IMUL r, r/m, imm */
        modrm(c);
        int16_t a = (int16_t)getE(c, 1);
        int16_t b = op == 0x69 ? (int16_t)fetch16(c) : (int16_t)(int8_t)fetch8(c);
        int32_t r = (int32_t)a * b;
        setG(c, 1, (uint16_t)r);
        setf(c, CF | OF, r != (int16_t)r);
        break;
    }
    case 0x6C: case 0x6D: case 0x6E: case 0x6F:
    case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB:
    case 0xAC: case 0xAD: case 0xAE: case 0xAF:
        string_op(c, op);
        break;
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        int8_t d = (int8_t)fetch8(c);
        if (cond(c, op & 0x0F)) c->ip += (uint16_t)d;
        break;
    }
    case 0x80: case 0x81: case 0x82: case 0x83: {   /* group 1 */
        int w = op & 1;
        modrm(c);
        uint16_t a = getE(c, w);
        uint16_t b = op == 0x81 ? fetch16(c) : op == 0x83 ? (uint16_t)(int8_t)fetch8(c) : fetch8(c);
        uint16_t r = alu(c, c->reg, a, b, w);
        if (c->reg != 7) setE(c, w, r);
        break;
    }
    case 0x84: case 0x85: modrm(c); alu(c, 4, getE(c, op & 1), getG(c, op & 1), op & 1); break;
    case 0x86: case 0x87: {
        int w = op & 1;
        modrm(c);
        uint16_t a = getE(c, w), b = getG(c, w);
        setE(c, w, b); setG(c, w, a);
        break;
    }
    case 0x88: case 0x89: modrm(c); setE(c, op & 1, getG(c, op & 1)); break;
    case 0x8A: case 0x8B: modrm(c); setG(c, op & 1, getE(c, op & 1)); break;
    case 0x8C: modrm(c); setE(c, 1, c->sreg[c->reg & 3]); break;
    case 0x8D: modrm(c); setG(c, 1, c->ea_off); break;
    case 0x8E:
        modrm(c);
        if ((c->reg & 3) != CPU86_CS) c->sreg[c->reg & 3] = getE(c, 1);
        if ((c->reg & 3) == CPU86_SS) c->inhibit = 1;
        break;
    case 0x8F: {
        uint16_t v = cpu86_pop(c);
        modrm(c);
        setE(c, 1, v);
        break;
    }
    case 0x90: break;
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
        uint16_t t = AX; AX = c->r.w[op & 7]; c->r.w[op & 7] = t;
        break;
    }
    case 0x98: AX = (uint16_t)(int8_t)AL; break;
    case 0x99: DX = (AX & 0x8000) ? 0xFFFF : 0; break;
    case 0x9A: {
        uint16_t ip = fetch16(c), cs = fetch16(c);
        cpu86_push(c, CS); cpu86_push(c, c->ip);
        CS = cs; c->ip = ip;
        break;
    }
    case 0x9B: break;                                /* WAIT */
    case 0x9C: cpu86_push(c, c->flags | FLAGS_FIXED); break;
    case 0x9D: c->flags = (uint16_t)((cpu86_pop(c) & 0x0FD5) | FLAGS_FIXED); break;
    case 0x9E: c->flags = (uint16_t)((c->flags & 0xFF00) | (AH & 0xD5) | 2); break;
    case 0x9F: AH = (uint8_t)c->flags; break;
    case 0xA0: AL = cpu86_rd8(c, dseg(c), fetch16(c)); break;
    case 0xA1: AX = cpu86_rd16(c, dseg(c), fetch16(c)); break;
    case 0xA2: cpu86_wr8(c, dseg(c), fetch16(c), AL); break;
    case 0xA3: cpu86_wr16(c, dseg(c), fetch16(c), AX); break;
    case 0xA8: alu(c, 4, AL, fetch8(c), 0); break;
    case 0xA9: alu(c, 4, AX, fetch16(c), 1); break;
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        *reg8(c, op & 7) = fetch8(c); break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        c->r.w[op & 7] = fetch16(c); break;
    case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
        int w = op & 1;
        modrm(c);
        uint16_t v = getE(c, w);
        uint8_t n = op <= 0xC1 ? fetch8(c) : op <= 0xD1 ? 1 : CL;
        setE(c, w, shift(c, c->reg, v, n, w));
        break;
    }
    case 0xC2: { uint16_t n = fetch16(c); c->ip = cpu86_pop(c); SP += n; break; }
    case 0xC3: c->ip = cpu86_pop(c); break;
    case 0xC4: case 0xC5:
        if (op == 0xC4 && cpu86_rd8(c, CS, c->ip) == 0xC4) {      /* BOP nn: into the BIOS */
            c->ip++;
            uint8_t n = fetch8(c);
            c->bop(c->machine, n);
            break;
        }
        modrm(c);
        setG(c, 1, cpu86_rd16(c, c->ea_seg, c->ea_off));
        c->sreg[op == 0xC4 ? CPU86_ES : CPU86_DS] = cpu86_rd16(c, c->ea_seg, (uint16_t)(c->ea_off + 2));
        break;
    case 0xC6: case 0xC7: {
        int w = op & 1;
        modrm(c);
        setE(c, w, w ? fetch16(c) : fetch8(c));
        break;
    }
    case 0xC8: {                                     /* ENTER */
        uint16_t size = fetch16(c);
        uint8_t level = fetch8(c) & 0x1F;
        cpu86_push(c, BP);
        uint16_t frame = SP;
        if (level) {
            for (int i = 1; i < level; i++) { BP -= 2; cpu86_push(c, cpu86_rd16(c, SS, BP)); }
            cpu86_push(c, frame);
        }
        BP = frame;
        SP -= size;
        break;
    }
    case 0xC9: SP = BP; BP = cpu86_pop(c); break;   /* LEAVE */
    case 0xCA: { uint16_t n = fetch16(c); c->ip = cpu86_pop(c); CS = cpu86_pop(c); SP += n; break; }
    case 0xCB: c->ip = cpu86_pop(c); CS = cpu86_pop(c); break;
    case 0xCC: cpu86_interrupt(c, 3); break;
    case 0xCD: { uint8_t n = fetch8(c); cpu86_interrupt(c, n); break; }
    case 0xCE: if (c->flags & OF) cpu86_interrupt(c, 4); break;
    case 0xCF:
        c->ip = cpu86_pop(c);
        CS = cpu86_pop(c);
        c->flags = (uint16_t)((cpu86_pop(c) & 0x0FD5) | FLAGS_FIXED);
        break;
    case 0xD4: {                                     /* AAM */
        uint8_t base = fetch8(c);
        if (!base) { divide_error(c); break; }
        AH = (uint8_t)(AL / base); AL = (uint8_t)(AL % base);
        szp(c, AL, 0);
        break;
    }
    case 0xD5: {                                     /* AAD */
        uint8_t base = fetch8(c);
        AL = (uint8_t)(AL + AH * base); AH = 0;
        szp(c, AL, 0);
        break;
    }
    case 0xD6: AL = (c->flags & CF) ? 0xFF : 0; break;      /* SALC */
    case 0xD7: AL = cpu86_rd8(c, dseg(c), (uint16_t)(BX + AL)); break;
    case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        modrm(c);                                    /* no coprocessor */
        break;
    case 0xE0: case 0xE1: case 0xE2: {               /* LOOPNZ LOOPZ LOOP */
        int8_t d = (int8_t)fetch8(c);
        CX--;
        int z = (c->flags & ZF) != 0;
        if (CX && (op == 0xE2 || (op == 0xE1 && z) || (op == 0xE0 && !z))) c->ip += (uint16_t)d;
        break;
    }
    case 0xE3: { int8_t d = (int8_t)fetch8(c); if (!CX) c->ip += (uint16_t)d; break; }
    case 0xE4: AL = c->in8(c->machine, fetch8(c)); break;
    case 0xE5: { uint8_t p = fetch8(c); AL = c->in8(c->machine, p); AH = c->in8(c->machine, (uint16_t)(p + 1)); break; }
    case 0xE6: c->out8(c->machine, fetch8(c), AL); break;
    case 0xE7: { uint8_t p = fetch8(c); c->out8(c->machine, p, AL); c->out8(c->machine, (uint16_t)(p + 1), AH); break; }
    case 0xE8: { uint16_t d = fetch16(c); cpu86_push(c, c->ip); c->ip += d; break; }
    case 0xE9: { uint16_t d = fetch16(c); c->ip += d; break; }
    case 0xEA: { uint16_t ip = fetch16(c), cs = fetch16(c); c->ip = ip; CS = cs; break; }
    case 0xEB: { int8_t d = (int8_t)fetch8(c); c->ip += (uint16_t)d; break; }
    case 0xEC: AL = c->in8(c->machine, DX); break;
    case 0xED: AL = c->in8(c->machine, DX); AH = c->in8(c->machine, (uint16_t)(DX + 1)); break;
    case 0xEE: c->out8(c->machine, DX, AL); break;
    case 0xEF: c->out8(c->machine, DX, AL); c->out8(c->machine, (uint16_t)(DX + 1), AH); break;
    case 0xF4: c->halted = 1; break;
    case 0xF5: c->flags ^= CF; break;
    case 0xF6: case 0xF7: modrm(c); group3(c, op & 1); break;
    case 0xF8: c->flags &= (uint16_t)~CF; break;
    case 0xF9: c->flags |= CF; break;
    case 0xFA: c->flags &= (uint16_t)~IF; break;
    case 0xFB: c->flags |= IF; c->inhibit = 1; break;
    case 0xFC: c->flags &= (uint16_t)~DF; break;
    case 0xFD: c->flags |= DF; break;
    case 0xFE: {
        modrm(c);
        if (c->reg < 2) setE(c, 0, incdec(c, getE(c, 0), c->reg, 0));
        break;
    }
    case 0xFF: {
        modrm(c);
        uint16_t v = getE(c, 1);
        switch (c->reg) {
        case 0: case 1: setE(c, 1, incdec(c, v, c->reg, 1)); break;
        case 2: cpu86_push(c, c->ip); c->ip = v; break;
        case 3: {
            uint16_t cs = cpu86_rd16(c, c->ea_seg, (uint16_t)(c->ea_off + 2));
            cpu86_push(c, CS); cpu86_push(c, c->ip);
            c->ip = v; CS = cs;
            break;
        }
        case 4: c->ip = v; break;
        case 5: CS = cpu86_rd16(c, c->ea_seg, (uint16_t)(c->ea_off + 2)); c->ip = v; break;
        case 6: if (c->mod == 3 && c->rm == CPU86_SP) cpu86_push(c, (uint16_t)(v - 2)); else cpu86_push(c, v); break;
        default: c->ip = c->start_ip; cpu86_interrupt(c, 6); break;
        }
        break;
    }
    default:                                         /* invalid opcode (0F, 63-67, F1): INT 6 */
        c->ip = c->start_ip;
        cpu86_interrupt(c, 6);
        break;
    }
}

void cpu86_run(Cpu86 *c, uint32_t n)
{
    while (n--) {
        if (!c->inhibit && (c->flags & IF)) {
            int v = c->irq(c->machine);
            if (v >= 0) {
                c->ack(c->machine, v);
                c->halted = 0;
                cpu86_interrupt(c, (uint8_t)v);
            }
        }
        c->inhibit = 0;
        if (c->halted) return;
        int trap = (c->flags & TF) != 0;              /* (TF as the instruction started) */
        step(c);
        c->count++;
        if (trap && !c->inhibit) cpu86_interrupt(c, 1);
    }
}
