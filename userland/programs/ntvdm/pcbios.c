/* pcbios.c — the PC and its BIOS for the real-mode CPU (see pcbios.h) */
#include "pcbios.h"

#include <string.h>

#define BDA 0x40                           /* the BIOS data area's segment */
#define ROM 0xF000
#define STUBS 0x1000                       /* ROM offset of vector n's stub: STUBS + n * 16 */
#define DPT 0xEFC7                         /* the diskette parameter table (INT 1Eh) */
#define IRET_AT 0xFF53                     /* a lone IRET, the vectors nobody serves */
#define EOI_AT 0xFF40                      /* EOI to the first PIC and IRET */
#define EOI2_AT 0xFF48                     /* EOI to both PICs and IRET */
#define TICK_MS (1000.0 / 18.2065)
#define TICKS_PER_DAY 0x1800B0

#define AX c->r.w[CPU86_AX]
#define BX c->r.w[CPU86_BX]
#define CX c->r.w[CPU86_CX]
#define DX c->r.w[CPU86_DX]
#define AL c->r.b[0]
#define AH c->r.b[1]
#define BL c->r.b[6]
#define BH c->r.b[7]
#define CL c->r.b[2]
#define CH c->r.b[3]
#define DL c->r.b[4]
#define DH c->r.b[5]

static uint8_t bda8(Vdm *v, uint16_t off) { return cpu86_rd8(&v->cpu, BDA, off); }
static uint16_t bda16(Vdm *v, uint16_t off) { return cpu86_rd16(&v->cpu, BDA, off); }
static void set_bda8(Vdm *v, uint16_t off, uint8_t x) { cpu86_wr8(&v->cpu, BDA, off, x); }
static void set_bda16(Vdm *v, uint16_t off, uint16_t x) { cpu86_wr16(&v->cpu, BDA, off, x); }

/* a flag in the FLAGS the interrupt pushed (IRET restores it): the BIOS's
 * answers in CF and ZF */
static void ret_flag(Vdm *v, uint16_t f, int on)
{
    Cpu86 *c = &v->cpu;
    uint16_t sp = (uint16_t)(c->r.w[CPU86_SP] + 4);
    uint16_t fl = cpu86_rd16(c, c->sreg[CPU86_SS], sp);
    fl = on ? (uint16_t)(fl | f) : (uint16_t)(fl & ~f);
    cpu86_wr16(c, c->sreg[CPU86_SS], sp, fl);
}

static uint8_t bcd(int x) { return (uint8_t)(((x / 10) % 10) << 4 | (x % 10)); }

/* ---- video (INT 10h): colour text, 80 x 25, 8 pages at B800:0 ---- */

static int cols(Vdm *v) { int n = bda16(v, 0x4A); return n ? n : 80; }
static int rows(Vdm *v) { return bda8(v, 0x84) + 1; }
static uint32_t page_base(Vdm *v, int page) { (void)v; return 0xB8000 + (uint32_t)page * 0x1000; }

static void cell_put(Vdm *v, int page, int x, int y, uint8_t ch, int attr)
{
    uint32_t a = page_base(v, page) + (uint32_t)(y * cols(v) + x) * 2;
    v->cpu.mem[a] = ch;
    if (attr >= 0) v->cpu.mem[a + 1] = (uint8_t)attr;
}

static void scroll(Vdm *v, int page, int top, int left, int bottom, int right, int n, uint8_t attr, int up)
{
    int w = cols(v), h = rows(v);
    if (right >= w) right = w - 1;
    if (bottom >= h) bottom = h - 1;
    if (left > right || top > bottom) return;
    int lines = bottom - top + 1;
    if (n <= 0 || n > lines) n = lines;
    uint8_t *base = v->cpu.mem + page_base(v, page);
    for (int i = 0; i < lines; i++) {
        int y = up ? top + i : bottom - i;
        int from = up ? y + n : y - n;
        uint8_t *dst = base + (y * w + left) * 2;
        if (i < lines - n) memmove(dst, base + (from * w + left) * 2, (size_t)(right - left + 1) * 2);
        else for (int x = 0; x <= right - left; x++) { dst[x * 2] = ' '; dst[x * 2 + 1] = attr; }
    }
}

static void cursor_get(Vdm *v, int page, int *x, int *y)
{
    *x = bda8(v, (uint16_t)(0x50 + page * 2));
    *y = bda8(v, (uint16_t)(0x51 + page * 2));
}

static void cursor_set(Vdm *v, int page, int x, int y)
{
    set_bda8(v, (uint16_t)(0x50 + page * 2), (uint8_t)x);
    set_bda8(v, (uint16_t)(0x51 + page * 2), (uint8_t)y);
}

static void teletype(Vdm *v, int page, uint8_t ch)
{
    int x, y, w = cols(v), h = rows(v);
    if (v->host.tty) v->host.tty(v->host.ctx, ch);
    cursor_get(v, page, &x, &y);
    switch (ch) {
    case 7: return;
    case 8: if (x > 0) x--; break;
    case 10: y++; break;
    case 13: x = 0; break;
    default:
        cell_put(v, page, x, y, ch, -1);
        if (++x >= w) { x = 0; y++; }
        break;
    }
    if (y >= h) {
        uint32_t a = page_base(v, page) + (uint32_t)((h - 1) * w + x) * 2 + 1;
        scroll(v, page, 0, 0, h - 1, w - 1, 1, v->cpu.mem[a] ? v->cpu.mem[a] : 7, 1);
        y = h - 1;
    }
    cursor_set(v, page, x, y);
}

static void set_mode(Vdm *v, uint8_t mode)
{
    int keep = mode & 0x80;
    mode &= 0x7F;
    set_bda8(v, 0x49, mode == 7 ? 7 : 3);
    set_bda16(v, 0x4A, 80);
    set_bda16(v, 0x4C, 0x1000);
    set_bda16(v, 0x4E, 0);
    for (int p = 0; p < 8; p++) cursor_set(v, p, 0, 0);
    set_bda16(v, 0x60, 0x0607);
    set_bda8(v, 0x62, 0);
    set_bda16(v, 0x63, 0x3D4);
    set_bda8(v, 0x84, 24);
    set_bda16(v, 0x85, 16);
    if (!keep) for (int p = 0; p < 8; p++) scroll(v, p, 0, 0, 24, 79, 0, 7, 1);
}

static void int10(Vdm *v)
{
    Cpu86 *c = &v->cpu;
    int page = BH & 7, x, y;
    switch (AH) {
    case 0x00: set_mode(v, AL); break;
    case 0x01: set_bda16(v, 0x60, CX); break;
    case 0x02: cursor_set(v, page, DL, DH); break;
    case 0x03: cursor_get(v, page, &x, &y); DL = (uint8_t)x; DH = (uint8_t)y; CX = bda16(v, 0x60); break;
    case 0x05: set_bda8(v, 0x62, AL & 7); set_bda16(v, 0x4E, (uint16_t)((AL & 7) * 0x1000)); break;
    case 0x06: case 0x07:
        scroll(v, bda8(v, 0x62), CH, CL, DH, DL, AL, BH, AH == 6);
        break;
    case 0x08: {
        cursor_get(v, page, &x, &y);
        uint32_t a = page_base(v, page) + (uint32_t)(y * cols(v) + x) * 2;
        AL = c->mem[a]; AH = c->mem[a + 1];
        break;
    }
    case 0x09: case 0x0A: {
        cursor_get(v, page, &x, &y);
        int w = cols(v), h = rows(v);
        for (int i = 0; i < CX && y < h; i++) {
            cell_put(v, page, x, y, AL, AH == 9 ? BL : -1);
            if (++x >= w) { x = 0; y++; }
        }
        break;
    }
    case 0x0E: teletype(v, bda8(v, 0x62), AL); break;
    case 0x0F: AL = bda8(v, 0x49); AH = (uint8_t)cols(v); BH = bda8(v, 0x62); break;
    case 0x10: break;                           /* palette: the console has its own */
    case 0x11:
        if (AL == 0x30) { CX = 16; DL = (uint8_t)(rows(v) - 1); }
        break;
    case 0x12:
        if (BL == 0x10) { BH = 0; BL = 3; CX = 9; }  /* EGA/VGA, colour, 256 KB */
        break;
    case 0x13: {                                /* write string ES:BP */
        int n = CX, attr = BL, upd = AL & 1, pairs = AL & 2, ox, oy;
        uint16_t seg = c->sreg[CPU86_ES], off = c->r.w[CPU86_BP];
        cursor_get(v, page, &ox, &oy);
        cursor_set(v, page, DL, DH);
        for (int i = 0; i < n; i++) {
            uint8_t ch = cpu86_rd8(c, seg, off++);
            if (pairs) attr = cpu86_rd8(c, seg, off++);
            if (ch == 7 || ch == 8 || ch == 10 || ch == 13) { teletype(v, page, ch); continue; }
            cursor_get(v, page, &x, &y);
            cell_put(v, page, x, y, ch, attr);
            int w = cols(v);
            if (++x >= w) { x = 0; y++; }
            if (y >= rows(v)) { teletype(v, page, 10); y = rows(v) - 1; }
            cursor_set(v, page, x, y);
        }
        if (!upd) cursor_set(v, page, ox, oy);
        break;
    }
    case 0x1A:
        if (AL == 0) { AL = 0x1A; BX = 0x0008; }   /* VGA, colour */
        break;
    default: break;
    }
}

/* ---- disk (INT 13h): drive A: is the floppy image ---- */

static int geometry(Vdm *v, uint32_t size)
{
    static const struct { uint32_t size; int cyls, heads, spt, type; } g[] = {
        { 163840, 40, 1, 8, 1 }, { 184320, 40, 1, 9, 1 }, { 327680, 40, 2, 8, 1 },
        { 368640, 40, 2, 9, 1 }, { 737280, 80, 2, 9, 3 }, { 1228800, 80, 2, 15, 2 },
        { 1474560, 80, 2, 18, 4 }, { 2949120, 80, 2, 36, 6 },
    };
    for (unsigned i = 0; i < sizeof(g) / sizeof(g[0]); i++)
        if (g[i].size == size) {
            v->cyls = g[i].cyls; v->heads = g[i].heads; v->spt = g[i].spt; v->drive_type = g[i].type;
            return 1;
        }
    return 0;
}

static void disk_status(Vdm *v, uint8_t st)
{
    Cpu86 *c = &v->cpu;
    AH = st;
    set_bda8(v, 0x41, st);
    ret_flag(v, CPU86_CF, st != 0);
}

static void int13(Vdm *v)
{
    Cpu86 *c = &v->cpu;
    int drive = DL;
    if (AH == 0x00) { disk_status(v, 0); return; }
    if (AH == 0x01) { AH = bda8(v, 0x41); ret_flag(v, CPU86_CF, AH != 0); return; }
    if (drive != 0 || !v->disk) {
        if (AH == 0x08) { DL = drive & 0x80 ? 0 : 1; }
        if (AH == 0x15) { AH = 0; ret_flag(v, CPU86_CF, 0); return; }
        disk_status(v, drive & 0x80 ? 0x01 : 0x80);
        return;
    }
    switch (AH) {
    case 0x02: case 0x03: case 0x04: {
        int cyl = CH | (CL & 0xC0) << 2, sec = CL & 0x3F, head = DH, n = AL;
        if (!sec || sec > v->spt || head >= v->heads || cyl >= v->cyls) { AL = 0; disk_status(v, 0x04); return; }
        uint32_t lba = ((uint32_t)cyl * v->heads + head) * v->spt + (sec - 1);
        uint16_t seg = c->sreg[CPU86_ES], off = BX;
        int done = 0;
        for (; done < n; done++, lba++) {
            if ((lba + 1) * 512 > v->disk_size) break;
            uint8_t *s = v->disk + lba * 512;
            for (int i = 0; i < 512; i++, off++) {
                if (AH == 0x02) cpu86_wr8(c, seg, off, s[i]);
                else if (AH == 0x03) s[i] = cpu86_rd8(c, seg, off);
            }
        }
        AL = (uint8_t)done;
        disk_status(v, done == n ? 0 : 0x04);
        break;
    }
    case 0x05: disk_status(v, 0); break;
    case 0x08:
        AX = 0; BL = (uint8_t)v->drive_type;
        CH = (uint8_t)(v->cyls - 1);
        CL = (uint8_t)(v->spt | ((v->cyls - 1) >> 8) << 6);
        DH = (uint8_t)(v->heads - 1); DL = 1;
        c->sreg[CPU86_ES] = ROM; c->r.w[CPU86_DI] = DPT;
        ret_flag(v, CPU86_CF, 0);
        break;
    case 0x15: AH = 0x01; ret_flag(v, CPU86_CF, 0); break;      /* a floppy drive, no change line */
    case 0x16: disk_status(v, 0); break;
    case 0x17: case 0x18: disk_status(v, 0); break;
    default: disk_status(v, 0x01); break;
    }
}

/* ---- keyboard (INT 16h): the BIOS's buffer at 40:1E, head 40:1A, tail 40:1C ---- */

int vdm_key(Vdm *v, uint16_t key)
{
    uint16_t head = bda16(v, 0x1A), tail = bda16(v, 0x1C);
    uint16_t start = bda16(v, 0x80), end = bda16(v, 0x82);
    uint16_t next = (uint16_t)(tail + 2);
    if (next >= end) next = start;
    if (next == head) return 0;                 /* (full: a real BIOS beeps) */
    set_bda16(v, tail, key);
    set_bda16(v, 0x1C, next);
    return 1;
}

uint16_t vdm_char_key(uint8_t ch)
{
    static const char rows[] =                  /* scan codes 02h-35h, unshifted and shifted */
        "1234567890-=\b\tqwertyuiop[]\r\0asdfghjkl;'`\0\\zxcvbnm,./";
    static const char shifted[] =
        "!@#$%^&*()_+\b\tQWERTYUIOP{}\r\0ASDFGHJKL:\"~\0|ZXCVBNM<>?";
    if (ch == ' ') return 0x3920;
    if (ch == 0x1B) return 0x011B;
    if (ch >= 1 && ch <= 26 && ch != 8 && ch != 9 && ch != 13)      /* Ctrl+letter */
        return (uint16_t)((vdm_char_key((uint8_t)('a' + ch - 1)) & 0xFF00) | ch);
    for (int i = 0; i < (int)sizeof(rows) - 1; i++)
        if (ch && (rows[i] == (char)ch || shifted[i] == (char)ch)) return (uint16_t)((i + 2) << 8 | ch);
    return ch;                                  /* (no key for it: the character alone) */
}

void vdm_shift_state(Vdm *v, uint8_t flags) { set_bda8(v, 0x17, flags); }

static int key_peek(Vdm *v, uint16_t *key, int take)
{
    uint16_t head = bda16(v, 0x1A), tail = bda16(v, 0x1C);
    if (head == tail) return 0;
    *key = bda16(v, head);
    if (take) {
        head += 2;
        if (head >= bda16(v, 0x82)) head = bda16(v, 0x80);
        set_bda16(v, 0x1A, head);
    }
    return 1;
}

/* the keys of the 101-key keyboard (E0 in the low byte) are the 84-key
 * ones (0) for the original calls, AH=00h and 01h */
static uint16_t old_key(uint16_t k) { return (k & 0xFF) == 0xE0 ? (uint16_t)(k & 0xFF00) : k; }

static void int16(Vdm *v)
{
    Cpu86 *c = &v->cpu;
    uint16_t key;
    switch (AH) {
    case 0x00: case 0x10:
        if (!key_peek(v, &key, 1)) {
            c->ip -= 3;                         /* (run the BOP again: wait for a key) */
            c->flags |= CPU86_IF;               /* (with the timer running, as a BIOS's STI) */
            v->wait = VDM_WAIT;
            return;
        }
        v->polls = 0;
        AX = AH == 0x00 ? old_key(key) : key;
        break;
    case 0x01: case 0x11:
        if (key_peek(v, &key, 0)) {
            v->polls = 0;
            AX = AH == 0x01 ? old_key(key) : key;
            ret_flag(v, CPU86_ZF, 0);
        } else {
            ret_flag(v, CPU86_ZF, 1);
            if (++v->polls >= 64) { v->polls = 0; v->wait = VDM_IDLE; }
        }
        break;
    case 0x02: AL = bda8(v, 0x17); break;
    case 0x12: AL = bda8(v, 0x17); AH = bda8(v, 0x18); break;
    case 0x05: vdm_key(v, CX); AL = 0; break;
    default: break;
    }
}

/* ---- clock (INT 1Ah) ---- */

static void int1a(Vdm *v)
{
    Cpu86 *c = &v->cpu;
    int t[6];
    switch (AH) {
    case 0x00:
        CX = bda16(v, 0x6E); DX = bda16(v, 0x6C);
        AL = bda8(v, 0x70); set_bda8(v, 0x70, 0);
        break;
    case 0x01: set_bda16(v, 0x6E, CX); set_bda16(v, 0x6C, DX); set_bda8(v, 0x70, 0); break;
    case 0x02:
        v->host.localtime(v->host.ctx, t);
        CH = bcd(t[3]); CL = bcd(t[4]); DH = bcd(t[5]); DL = 0;
        ret_flag(v, CPU86_CF, 0);
        break;
    case 0x04:
        v->host.localtime(v->host.ctx, t);
        CH = bcd(t[0] / 100); CL = bcd(t[0] % 100); DH = bcd(t[1]); DL = bcd(t[2]);
        ret_flag(v, CPU86_CF, 0);
        break;
    case 0x03: case 0x05: ret_flag(v, CPU86_CF, 0); break;   /* (the host's clock stays) */
    default: ret_flag(v, CPU86_CF, 1); break;
    }
}

static void timer_tick(Vdm *v)
{
    uint32_t t = (uint32_t)bda16(v, 0x6C) | (uint32_t)bda16(v, 0x6E) << 16;
    if (++t >= TICKS_PER_DAY) { t = 0; set_bda8(v, 0x70, 1); }
    set_bda16(v, 0x6C, (uint16_t)t);
    set_bda16(v, 0x6E, (uint16_t)(t >> 16));
}

static void set_ticks_from_clock(Vdm *v)
{
    int t[6];
    v->host.localtime(v->host.ctx, t);
    uint32_t ticks = (uint32_t)((t[3] * 3600 + t[4] * 60 + t[5]) * 18.2065);
    set_bda16(v, 0x6C, (uint16_t)ticks);
    set_bda16(v, 0x6E, (uint16_t)(ticks >> 16));
}

/* ---- booting (INT 19h) ---- */

static void boot(Vdm *v)
{
    Cpu86 *c = &v->cpu;
    for (int i = 0; i < 512; i++) c->mem[0x7C00 + i] = v->disk[i];
    c->sreg[CPU86_CS] = c->sreg[CPU86_DS] = c->sreg[CPU86_ES] = c->sreg[CPU86_SS] = 0;
    c->ip = 0x7C00;
    c->r.w[CPU86_SP] = 0x7C00;
    DL = 0;
    c->flags = 0xF202;
}

static void bop(void *machine, uint8_t n)
{
    Vdm *v = machine;
    Cpu86 *c = &v->cpu;
    switch (n) {
    case 0x08: timer_tick(v); break;
    case 0x10: int10(v); break;
    case 0x11: AX = bda16(v, 0x10); break;
    case 0x12: AX = bda16(v, 0x13); break;
    case 0x13: int13(v); break;
    case 0x14:                                  /* COM1: transmit only */
        if (DX != 0) { AH = 0x80; break; }
        switch (AH) {
        case 0x01: v->host.serial(v->host.ctx, AL); AH = 0x60; break;
        case 0x02: AH = 0x80; break;            /* (nothing comes in: time-out) */
        default: AX = 0x6000; break;
        }
        break;
    case 0x15:
        switch (AH) {
        case 0x4F: ret_flag(v, CPU86_CF, 1); break;          /* (keyboard intercept: use the key) */
        case 0x88: AX = 0; ret_flag(v, CPU86_CF, 0); break;  /* no extended memory */
        case 0x90: case 0x91: AH = 0; ret_flag(v, CPU86_CF, 0); break;
        default: AH = 0x86; ret_flag(v, CPU86_CF, 1); break;
        }
        break;
    case 0x16: int16(v); break;
    case 0x17: AH = 0x90; break;                /* printer: ready, selected */
    case 0x19: boot(v); break;
    case 0x1A: int1a(v); break;
    default: break;
    }
}

/* ---- I/O ports ---- */

static uint8_t cmos(Vdm *v, uint8_t reg)
{
    int t[6];
    v->host.localtime(v->host.ctx, t);
    switch (reg & 0x7F) {
    case 0x00: return bcd(t[5]);
    case 0x02: return bcd(t[4]);
    case 0x04: return bcd(t[3]);
    case 0x07: return bcd(t[2]);
    case 0x08: return bcd(t[1]);
    case 0x09: return bcd(t[0] % 100);
    case 0x0A: return 0x26;
    case 0x0B: return 0x02;                     /* 24 hours, BCD */
    case 0x0D: return 0x80;                     /* battery good */
    case 0x10: return (uint8_t)(v->drive_type << 4);
    case 0x14: return 0x21;                     /* a floppy drive, colour 80 columns */
    case 0x15: return 0x80; case 0x16: return 0x02;    /* 640 KB */
    case 0x32: return bcd(t[0] / 100);
    default: return 0;
    }
}

static uint8_t in8(void *machine, uint16_t port)
{
    Vdm *v = machine;
    switch (port) {
    case 0x20: return v->pic_irr;
    case 0x21: return v->pic_mask;
    case 0x40: return (uint8_t)(v->pit_count -= 0x101);  /* (a counter that moves) */
    case 0x60: return 0;
    case 0x61: return v->port61 ^= 0x10;        /* (refresh toggles: delay loops see it change) */
    case 0x64: return 0x14;
    case 0x71: return cmos(v, v->cmos_index);
    case 0x3DA: return (uint8_t)(v->cpu.count & 0x10 ? 0x09 : 0x00);   /* (retrace comes and goes) */
    case 0x3D5: return 0;
    case 0x3FD: return 0x60;                    /* COM1: transmitter empty */
    default: return 0xFF;
    }
}

static void out8(void *machine, uint16_t port, uint8_t x)
{
    Vdm *v = machine;
    switch (port) {
    case 0x20:
        if (x == 0x20) v->pic_isr &= (uint8_t)(v->pic_isr - 1);   /* (EOI: the highest in service) */
        break;
    case 0x21: v->pic_mask = x; break;
    case 0x61: v->port61 = (uint8_t)((v->port61 & 0x10) | (x & ~0x10)); break;
    case 0x70: v->cmos_index = x; break;
    case 0x3D4: v->crtc_index = x; break;
    case 0x3F8: v->host.serial(v->host.ctx, x); break;
    default: break;
    }
}

/* the timer interrupt (IRQ 0, vector 8) when it is due and not masked or
 * already in service */
static int irq(void *machine)
{
    Vdm *v = machine;
    if ((v->pic_irr & 1) && !(v->pic_mask & 1) && !(v->pic_isr & 1)) return 8;
    return -1;
}

static void ack(void *machine, int vector)
{
    Vdm *v = machine;
    (void)vector;
    v->pic_irr &= (uint8_t)~1;
    v->pic_isr |= 1;
}

/* ---- the ROM and the machine ---- */

static void rom(Vdm *v, uint16_t off, const uint8_t *code, int n)
{
    for (int i = 0; i < n; i++) v->cpu.mem[(ROM << 4) + off + i] = code[i];
}

static void vector(Vdm *v, int n, uint16_t off)
{
    uint8_t *ivt = v->cpu.mem + n * 4;
    ivt[0] = (uint8_t)off; ivt[1] = (uint8_t)(off >> 8);
    ivt[2] = (uint8_t)ROM; ivt[3] = (uint8_t)(ROM >> 8);
}

static void bios_stub(Vdm *v, int n)
{
    uint8_t code[] = { 0xC4, 0xC4, (uint8_t)n, 0xCF };     /* BOP n; IRET */
    rom(v, (uint16_t)(STUBS + n * 16), code, sizeof(code));
    vector(v, n, (uint16_t)(STUBS + n * 16));
}

int vdm_init(Vdm *v, uint8_t *mem, uint8_t *disk, uint32_t size, const VdmHost *host)
{
    memset(v, 0, sizeof(*v));
    if (!geometry(v, size)) return 0;
    v->disk = disk;
    v->disk_size = size;
    v->host = *host;
    v->cpu.mem = mem;
    memset(mem, 0, CPU86_MEM_SIZE);
    cpu86_reset(&v->cpu);
    v->cpu.machine = v;
    v->cpu.in8 = in8;
    v->cpu.out8 = out8;
    v->cpu.bop = bop;
    v->cpu.irq = irq;
    v->cpu.ack = ack;

    static const uint8_t iret[] = { 0xCF };
    static const uint8_t eoi[] = { 0x50, 0xB0, 0x20, 0xE6, 0x20, 0x58, 0xCF };
    static const uint8_t eoi2[] = { 0x50, 0xB0, 0x20, 0xE6, 0xA0, 0xE6, 0x20, 0x58, 0xCF };
    /* the timer: BOP 08 (count the tick); INT 1Ch; EOI; IRET */
    static const uint8_t timer[] = { 0xC4, 0xC4, 0x08, 0xCD, 0x1C, 0x50, 0xB0, 0x20, 0xE6, 0x20, 0x58, 0xCF };
    static const uint8_t dpt[] = { 0xDF, 0x02, 0x25, 0x02, 0x12, 0x1B, 0xFF, 0x6C, 0xF6, 0x0F, 0x08 };
    rom(v, IRET_AT, iret, sizeof(iret));
    rom(v, EOI_AT, eoi, sizeof(eoi));
    rom(v, EOI2_AT, eoi2, sizeof(eoi2));
    for (int n = 0; n < 256; n++) vector(v, n, IRET_AT);
    for (int n = 0x08; n < 0x10; n++) vector(v, n, EOI_AT);
    for (int n = 0x70; n < 0x78; n++) vector(v, n, EOI2_AT);
    rom(v, (uint16_t)(STUBS + 8 * 16), timer, sizeof(timer));
    vector(v, 8, (uint16_t)(STUBS + 8 * 16));
    static const int served[] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x19, 0x1A };
    for (unsigned i = 0; i < sizeof(served) / sizeof(served[0]); i++) bios_stub(v, served[i]);
    rom(v, DPT, dpt, sizeof(dpt));
    uint8_t dpt_spt = (uint8_t)v->spt;
    rom(v, DPT + 4, &dpt_spt, 1);
    vector(v, 0x1E, DPT);
    vector(v, 0x1D, 0xF0A4);                    /* (video parameters: unused, zeros) */
    for (int i = 0; i < 4; i++) mem[0x1F * 4 + i] = 0;
    static const uint8_t date[] = "01/01/92";
    rom(v, 0xFFF5, date, 8);
    static const uint8_t model[] = { 0xFC, 0x00 };   /* an AT */
    rom(v, 0xFFFE, model, 2);
    static const uint8_t reset[] = { 0xCD, 0x19 };   /* (F000:FFF0: INT 19h) */
    rom(v, 0xFFF0, reset, 2);

    set_bda16(v, 0x10, 0x0021);                 /* one floppy drive, colour 80 x 25 */
    set_bda16(v, 0x13, 640);
    set_bda16(v, 0x1A, 0x1E);
    set_bda16(v, 0x1C, 0x1E);
    set_bda16(v, 0x80, 0x1E);
    set_bda16(v, 0x82, 0x3E);
    set_bda8(v, 0x75, 0);                       /* no hard disks */
    set_bda8(v, 0x96, 0x10);                    /* a 101-key keyboard */
    set_mode(v, 3);
    set_ticks_from_clock(v);
    v->pic_mask = 0xFC;                         /* (timer and keyboard) */
    v->next_tick = (double)v->host.ms(v->host.ctx) + TICK_MS;
    boot(v);
    return 1;
}

double vdm_next_tick(Vdm *v) { return v->next_tick; }

int vdm_run(Vdm *v, uint32_t n)
{
    uint64_t end = v->cpu.count + n;
    while (v->cpu.count < end) {
        double now = (double)v->host.ms(v->host.ctx);
        if (now >= v->next_tick) {
            v->pic_irr |= 1;
            v->next_tick += TICK_MS;
            if (now - v->next_tick > 10 * TICK_MS) v->next_tick = now + TICK_MS;   /* (host was away) */
        }
        uint64_t chunk = v->cpu.count + 4096 < end ? v->cpu.count + 4096 : end;
        v->wait = VDM_RAN;
        while (v->cpu.count < chunk && v->wait == VDM_RAN) {
            cpu86_run(&v->cpu, 1);
            if (v->cpu.halted) {
                cpu86_run(&v->cpu, 1);          /* (an interrupt due wakes it) */
                if (v->cpu.halted) return VDM_WAIT;
            }
        }
        if (v->wait != VDM_RAN) return v->wait;
    }
    return VDM_RAN;
}

const uint8_t *vdm_text(Vdm *v, int *c, int *r)
{
    *c = cols(v);
    *r = rows(v);
    return v->cpu.mem + page_base(v, bda8(v, 0x62));
}

void vdm_cursor(Vdm *v, int *x, int *y, int *visible)
{
    cursor_get(v, bda8(v, 0x62), x, y);
    uint16_t shape = bda16(v, 0x60);
    *visible = !(shape & 0x2000) && (shape >> 8) <= (shape & 0x1F);
}
