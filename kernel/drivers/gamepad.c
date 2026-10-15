/*
 * gamepad.c — game controllers: the slots programs read, and HID gamepads
 *
 * Each controller a driver finds gets a slot (gamepad.h) holding what it
 * is and its latest state; programs read them through CTL_GAMEPAD
 * (um_gui.c), xinput1_4.dll the Xbox controllers (xpad.c) and dinput8.dll
 * all of them.  Xbox controllers get an XInput user index (0-3) when
 * they are plugged in: the lowest one free, as Windows gives them.
 *
 * HID gamepads and joysticks (an application collection of Generic
 * Desktop usage 4, Joystick, or 5, Game Pad: most controllers that are
 * not Xbox ones, flight sticks, wheels) come through the HID class
 * driver, as on Windows: HidUsb under hidclass moves their reports, and
 * NovaOS's game controller class driver "gamehid" (nt/kbdhid.c), which
 * Plug and Play starts on a HID_DEVICE_SYSTEM_GAME collection, hands the
 * collection's report descriptor and each input report here
 * (PadHidOpen, PadHidInput).  The report descriptor (HID 1.11, section
 * 6.2.2) says where the buttons (Button page usages 1-32), the axes
 * (Generic Desktop X, Y, Z, Rx, Ry, Rz, Slider, Dial) and the hat switch
 * are, and each report becomes the state DirectInput shows: axes from 0
 * to 65535 over their logical range, the hat in hundredths of a degree.
 * A D-pad given as Generic Desktop D-pad Up/Down/Right/Left buttons
 * (usages 0x90-0x93) becomes the hat too.  Raw Input and hid.dll read
 * such a controller's reports from its collection, through hidclass;
 * the raw side kept here (the report ring, um_hid.c's paths) is the Xbox
 * controllers' HID side only.
 */

#include "gamepad.h"
#include "../mm/vmm.h"
#include "../lib/string.h"
#include "../ke/printf.h"
#include "../ke/spinlock.h"
#include "../ke/waitq.h"
#include "../ke/scheduler.h"

/* ---------------------------------------------------------------------------
 * The slots
 * ------------------------------------------------------------------------- */

typedef struct {
    bool      used;
    PadInfo   info;
    PadState  st;
    PadRumble rumble;
    void     *ctx;
    UINT16    rl, rr;                 /* the motors as last set */
    UINT8    *desc;                   /* its HID report descriptor (an Xbox controller's: g_xbox_desc) */
    UINT16    desc_len, in_len;
    PadRaw    last;                   /* its latest report */
} Pad;

static Pad        g_pads[PAD_SLOTS];
static KSpinLock  g_pad_lock = KSPINLOCK_INIT;
static UINT32     g_serial;
static PadRaw     g_raw[PAD_RAW_RING];
static UINT32     g_raw_seq;          /* the newest report's seq */
static UINT32     g_changes;          /* controllers plugged in or out */
static WaitQueue  g_rawq = WAITQ_INIT;
static PadRawSink g_sink;

/* The HID side Windows gives an Xbox controller (its Xbox driver's HID
 * collection, "IG_" in the device path): a game pad of X, Y (left stick),
 * Rx, Ry (right stick) and Z (left trigger up from 32768, right down),
 * 16 bits each, buttons 1-10 (A B X Y LB RB Back Start, the sticks) and
 * a hat (1-8 clockwise from up, 0 centred) */
static const UINT8 g_xbox_desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,                   /* Generic Desktop, Game Pad, Application */
    0xA1, 0x00, 0x09, 0x30, 0x09, 0x31,                   /*   Physical: X, Y */
    0x15, 0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00,             /*     0..65535 */
    0x95, 0x02, 0x75, 0x10, 0x81, 0x02, 0xC0,             /*     2 x 16 bits, Input (Data, Var, Abs) */
    0xA1, 0x00, 0x09, 0x33, 0x09, 0x34,                   /*   Physical: Rx, Ry */
    0x95, 0x02, 0x75, 0x10, 0x81, 0x02, 0xC0,
    0xA1, 0x00, 0x09, 0x32,                               /*   Physical: Z */
    0x95, 0x01, 0x75, 0x10, 0x81, 0x02, 0xC0,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x0A,                   /*   Buttons 1-10 */
    0x15, 0x00, 0x25, 0x01, 0x95, 0x0A, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x06, 0x81, 0x03,                               /*   6 bits padding */
    0x05, 0x01, 0x09, 0x39,                               /*   Hat switch: 1..8, 0..315 degrees, null state */
    0x15, 0x01, 0x25, 0x08, 0x35, 0x00, 0x46, 0x3B, 0x01, 0x65, 0x14,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
    0x15, 0x00, 0x25, 0x00, 0x35, 0x00, 0x45, 0x00, 0x65, 0x00,
    0x81, 0x03,                                           /*   4 bits padding */
    0xC0,
};
#define XBOX_REPORT_LEN 14

static bool is_xbox(const Pad *p) { return p->info.kind == PAD_XBOX360 || p->info.kind == PAD_XBOXONE; }

/* An Xbox controller's state as its HID report */
static void xbox_report(const PadState *st, UINT8 *r)
{
    const UINT16 ax[5] = { st->axis[PAD_X], st->axis[PAD_Y], st->axis[PAD_RX], st->axis[PAD_RY], st->axis[PAD_Z] };
    r[0] = 0;
    for (int i = 0; i < 5; i++) { r[1 + 2 * i] = (UINT8)ax[i]; r[2 + 2 * i] = (UINT8)(ax[i] >> 8); }
    r[11] = (UINT8)st->buttons;
    r[12] = (UINT8)((st->buttons >> 8) & 3);
    r[13] = st->pov < 0 ? 0 : (UINT8)(((st->pov + 2250) / 4500) % 8 + 1);
}

/* Into the ring (under g_pad_lock): @p's report @r of @len bytes */
static void raw_put(int slot, Pad *p, const UINT8 *r, int len)
{
    PadRaw *e = &p->last;
    memset(e, 0, sizeof(*e));
    e->seq = ++g_raw_seq;
    e->serial = p->info.serial;
    e->slot = (UINT8)slot;
    e->len = (UINT8)(len > PAD_REPORT_MAX ? PAD_REPORT_MAX : len);
    memcpy(e->data, r, e->len);
    g_raw[e->seq % PAD_RAW_RING] = *e;
}

int PadAttach(const PadInfo *info, PadRumble rumble, void *ctx)
{
    int slot = -1;
    IrqState s = spin_lock_irqsave(&g_pad_lock);
    for (int i = 0; i < PAD_SLOTS && slot < 0; i++) if (!g_pads[i].used) slot = i;
    if (slot >= 0) {
        Pad *p = &g_pads[slot];
        memset(p, 0, sizeof(*p));
        p->used = true;
        p->info = *info;
        p->info.serial = ++g_serial;
        p->info.xuser = -1;
        p->info.name[sizeof(p->info.name) - 1] = '\0';
        if (info->kind == PAD_XBOX360 || info->kind == PAD_XBOXONE) {
            for (int u = 0; u < PAD_XINPUT_USERS && p->info.xuser < 0; u++) {
                bool taken = false;
                for (int i = 0; i < PAD_SLOTS; i++)
                    if (i != slot && g_pads[i].used && g_pads[i].info.xuser == u) taken = true;
                if (!taken) p->info.xuser = (INT8)u;
            }
        }
        p->st.pov = -1;
        for (int a = 0; a < PAD_AXES; a++) p->st.axis[a] = 32768;
        p->rumble = info->rumble ? rumble : NULL;
        p->ctx = ctx;
        if (is_xbox(p)) {
            p->desc = (UINT8 *)g_xbox_desc;
            p->desc_len = sizeof(g_xbox_desc);
            p->in_len = XBOX_REPORT_LEN;
        }
        g_changes++;
    }
    spin_unlock_irqrestore(&g_pad_lock, s);
    if (slot >= 0) waitq_wake(&g_rawq);
    return slot;
}

void PadReport(int slot, const PadState *st)
{
    if (slot < 0 || slot >= PAD_SLOTS) return;
    IrqState s = spin_lock_irqsave(&g_pad_lock);
    Pad *p = &g_pads[slot];
    UINT8 r[XBOX_REPORT_LEN];
    bool raw = false;
    UINT32 serial = 0;
    if (p->used) {
        UINT32 packet = p->st.packet;
        p->st = *st;
        p->st.packet = packet + 1;
        if (is_xbox(p)) {                                     /* (a HID controller's are its collection's) */
            xbox_report(&p->st, r);
            raw_put(slot, p, r, sizeof(r));
            raw = true;
            serial = p->info.serial;
        }
    }
    PadRawSink sink = g_sink;
    spin_unlock_irqrestore(&g_pad_lock, s);
    if (raw) {
        waitq_wake(&g_rawq);
        if (sink) sink(slot, serial, r, sizeof(r));
    }
}

void PadDetach(int slot)
{
    if (slot < 0 || slot >= PAD_SLOTS) return;
    IrqState s = spin_lock_irqsave(&g_pad_lock);
    Pad *p = &g_pads[slot];
    UINT32 serial = p->used ? p->info.serial : 0;
    UINT8 *desc = p->desc != g_xbox_desc ? p->desc : NULL;
    memset(p, 0, sizeof(*p));
    if (serial) g_changes++;
    PadRawSink sink = g_sink;
    spin_unlock_irqrestore(&g_pad_lock, s);
    kfree(desc);
    if (serial) {
        waitq_wake(&g_rawq);
        if (sink) sink(slot, serial, NULL, -1);
    }
}

void PadRawInput(int slot, const void *data, int len)
{
    if ((slot != PAD_RAW_MOUSE && slot != PAD_RAW_KEYBOARD) || len <= 0 || len > PAD_REPORT_MAX) return;
    IrqState s = spin_lock_irqsave(&g_pad_lock);
    PadRaw *e = &g_raw[(g_raw_seq + 1) % PAD_RAW_RING];
    memset(e, 0, sizeof(*e));
    e->seq = ++g_raw_seq;
    e->serial = 1;
    e->slot = (UINT8)slot;
    e->len = (UINT8)len;
    memcpy(e->data, data, (size_t)len);
    spin_unlock_irqrestore(&g_pad_lock, s);
    waitq_wake(&g_rawq);
}

int PadGetDescriptor(int slot, UINT8 *out, int cap, UINT16 *in_len)
{
    if (slot < 0 || slot >= PAD_SLOTS) return 0;
    IrqState s = spin_lock_irqsave(&g_pad_lock);
    Pad *p = &g_pads[slot];
    int n = p->used && p->desc ? p->desc_len : 0;
    if (n > cap) n = cap;
    if (n) memcpy(out, p->desc, (size_t)n);
    if (in_len) *in_len = p->in_len;
    spin_unlock_irqrestore(&g_pad_lock, s);
    return n;
}

int PadSlotOfSerial(UINT32 serial)
{
    int slot = -1;
    IrqState s = spin_lock_irqsave(&g_pad_lock);
    for (int i = 0; i < PAD_SLOTS && slot < 0; i++)
        if (serial && g_pads[i].used && g_pads[i].info.serial == serial) slot = i;
    spin_unlock_irqrestore(&g_pad_lock, s);
    return slot;
}

int PadReadRaw(UINT32 after, UINT32 known_changes, PadRaw *out, int max, UINT64 wait_ticks,
               UINT32 *newest, UINT32 *changes)
{
    UINT64 until = sched_ticks() + wait_ticks;
    for (;;) {
        UINT32 gen = waitq_gen(&g_rawq);
        int n = 0;
        IrqState s = spin_lock_irqsave(&g_pad_lock);
        if (after < g_raw_seq) {
            UINT32 first = g_raw_seq >= PAD_RAW_RING ? g_raw_seq - PAD_RAW_RING + 1 : 1;
            if (after + 1 > first) first = after + 1;
            for (UINT32 k = first; k <= g_raw_seq && n < max; k++) out[n++] = g_raw[k % PAD_RAW_RING];
        }
        UINT32 ch = g_changes;
        if (newest) *newest = g_raw_seq;
        if (changes) *changes = ch;
        spin_unlock_irqrestore(&g_pad_lock, s);
        UINT64 now = sched_ticks();
        if (n || !max || ch != known_changes || now >= until) return n;
        waitq_wait(&g_rawq, gen, until - now);
    }
}

bool PadLastRaw(int slot, UINT8 id, PadRaw *out)
{
    if (slot < 0 || slot >= PAD_SLOTS) return false;
    IrqState s = spin_lock_irqsave(&g_pad_lock);
    Pad *p = &g_pads[slot];
    bool ok = p->used && p->last.seq && p->last.data[0] == id;
    if (ok) *out = p->last;
    spin_unlock_irqrestore(&g_pad_lock, s);
    return ok;
}

void PadSetRawSink(PadRawSink fn) { g_sink = fn; }

UINT32 PadPresent(void)
{
    UINT32 m = 0;
    IrqState s = spin_lock_irqsave(&g_pad_lock);
    for (int i = 0; i < PAD_SLOTS; i++) if (g_pads[i].used) m |= 1u << i;
    spin_unlock_irqrestore(&g_pad_lock, s);
    return m;
}

bool PadGetInfo(int slot, PadInfo *out)
{
    if (slot < 0 || slot >= PAD_SLOTS) return false;
    IrqState s = spin_lock_irqsave(&g_pad_lock);
    bool ok = g_pads[slot].used;
    if (ok) *out = g_pads[slot].info;
    spin_unlock_irqrestore(&g_pad_lock, s);
    return ok;
}

bool PadGetState(int slot, PadState *out)
{
    if (slot < 0 || slot >= PAD_SLOTS) return false;
    IrqState s = spin_lock_irqsave(&g_pad_lock);
    bool ok = g_pads[slot].used;
    if (ok) *out = g_pads[slot].st;
    spin_unlock_irqrestore(&g_pad_lock, s);
    return ok;
}

int PadXInputSlot(int user)
{
    int slot = -1;
    IrqState s = spin_lock_irqsave(&g_pad_lock);
    for (int i = 0; i < PAD_SLOTS && slot < 0; i++)
        if (g_pads[i].used && g_pads[i].info.xuser == user && user >= 0) slot = i;
    spin_unlock_irqrestore(&g_pad_lock, s);
    return slot;
}

/* Programs set the motors as often as every frame: only a change goes to
 * the controller */
bool PadSetRumble(int slot, UINT16 left, UINT16 right)
{
    if (slot < 0 || slot >= PAD_SLOTS) return false;
    IrqState s = spin_lock_irqsave(&g_pad_lock);
    Pad *p = &g_pads[slot];
    PadRumble fn = p->used ? p->rumble : NULL;
    void *ctx = p->ctx;
    bool same = p->used && p->rl == left && p->rr == right;
    if (p->used) { p->rl = left; p->rr = right; }
    bool used = p->used;
    spin_unlock_irqrestore(&g_pad_lock, s);
    if (!used) return false;
    if (!fn || same) return true;
    return fn(ctx, left, right);
}

/* ---------------------------------------------------------------------------
 * HID gamepads and joysticks
 * ------------------------------------------------------------------------- */

#define PAGE_DESKTOP 0x01
#define PAGE_BUTTON  0x09
#define MAX_FIELDS   64

typedef struct {
    UINT8  id;                        /* report ID (0: the device uses none) */
    UINT16 page, usage;
    UINT16 bit;                       /* after the ID byte */
    UINT8  size;
    INT32  lmin, lmax;
} PadField;

typedef struct {
    PadField f[MAX_FIELDS];
    int      n;
    bool     ids;
    int      slot;
    PadState last;
} HidPad;

static UINT32 hp_u(const UINT8 *p, int n)
{
    UINT32 v = 0;
    for (int i = 0; i < n; i++) v |= (UINT32)p[i] << (8 * i);
    return v;
}

static INT32 hp_s(const UINT8 *p, int n)
{
    UINT32 v = hp_u(p, n);
    if (n == 1) return (INT8)v;
    if (n == 2) return (INT16)v;
    return (INT32)v;
}

/* The fields a gamepad's reports carry that matter here, from its report
 * descriptor; *@app: the first application collection (page << 8 | usage) */
static void hp_parse(HidPad *h, const UINT8 *d, int len, UINT16 *app)
{
    struct { UINT16 page; INT32 lmin, lmax; UINT8 size, count, id; } g = { 0 }, stack[4];
    int sp = 0, depth = 0, nusage = 0;
    UINT32 usages[32], umin = 0, umax = 0;
    bool range = false;
    UINT16 bits[256];
    memset(bits, 0, sizeof(bits));
    *app = 0;
    for (int i = 0; i < len;) {
        UINT8 b = d[i];
        if (b == 0xFE) { if (i + 1 >= len) break; i += 3 + d[i + 1]; continue; }   /* long item */
        int n = (b & 3) == 3 ? 4 : (b & 3);
        if (i + 1 + n > len) break;
        const UINT8 *v = &d[i + 1];
        i += 1 + n;
        switch (b & 0xFC) {
        case 0x80: {                                             /* Input */
            UINT32 flags = hp_u(v, n);
            UINT16 *pos = &bits[g.id];
            if (!(flags & 1) && (flags & 2) && g.size)           /* variable data fields */
                for (int k = 0; k < g.count && h->n < MAX_FIELDS; k++) {
                    UINT32 u;
                    if (k < nusage) u = usages[k];
                    else if (range && umin + (UINT32)(k - nusage) <= umax) u = umin + (UINT32)(k - nusage);
                    else if (nusage) u = usages[nusage - 1];
                    else continue;
                    PadField *f = &h->f[h->n++];
                    f->id = g.id;
                    f->page = (u >> 16) ? (UINT16)(u >> 16) : g.page;
                    f->usage = (UINT16)u;
                    f->bit = (UINT16)(*pos + k * g.size);
                    f->size = g.size;
                    f->lmin = g.lmin; f->lmax = g.lmax;
                }
            *pos = (UINT16)(*pos + g.size * g.count);
            nusage = 0; range = false;
            break;
        }
        case 0x90: case 0xB0: nusage = 0; range = false; break; /* Output, Feature */
        case 0xA0:                                               /* Collection */
            if (depth == 0 && hp_u(v, n) == 1 && nusage && !*app)
                *app = (UINT16)((((usages[0] >> 16) ? (usages[0] >> 16) : g.page) << 8) | (usages[0] & 0xFF));
            depth++;
            nusage = 0; range = false;
            break;
        case 0xC0: if (depth) depth--; break;
        case 0x04: g.page = (UINT16)hp_u(v, n); break;
        case 0x14: g.lmin = hp_s(v, n); break;
        case 0x24:
            g.lmax = hp_s(v, n);
            if (g.lmin >= 0 && g.lmax < 0) g.lmax = (INT32)hp_u(v, n);   /* (meant unsigned) */
            break;
        case 0x74: g.size = (UINT8)hp_u(v, n); break;
        case 0x84: g.id = (UINT8)hp_u(v, n); h->ids = true; break;
        case 0x94: g.count = (UINT8)hp_u(v, n); break;
        case 0xA4: if (sp < 4) { stack[sp].page = g.page; stack[sp].lmin = g.lmin; stack[sp].lmax = g.lmax;
                                 stack[sp].size = g.size; stack[sp].count = g.count; stack[sp].id = g.id; sp++; } break;
        case 0xB4: if (sp) { sp--; g.page = stack[sp].page; g.lmin = stack[sp].lmin; g.lmax = stack[sp].lmax;
                             g.size = stack[sp].size; g.count = stack[sp].count; g.id = stack[sp].id; } break;
        case 0x08: if (nusage < 32) usages[nusage++] = n == 4 ? hp_u(v, n) : hp_u(v, n) | ((UINT32)g.page << 16); break;
        case 0x18: umin = n == 4 ? hp_u(v, n) : hp_u(v, n) | ((UINT32)g.page << 16); range = true; break;
        case 0x28: umax = n == 4 ? hp_u(v, n) : hp_u(v, n) | ((UINT32)g.page << 16); range = true; break;
        default: break;
        }
    }
}

static INT32 hp_bits(const UINT8 *r, int len, int bit, int size, bool sign)
{
    UINT32 v = 0;
    for (int k = 0; k < size && k < 32; k++) {
        int at = bit + k;
        if (at / 8 >= len) break;
        if (r[at / 8] & (1u << (at % 8))) v |= 1u << k;
    }
    if (sign && size < 32 && (v & (1u << (size - 1)))) v |= ~0u << size;
    return (INT32)v;
}

/* Up, right, down, left as a hat angle */
static INT32 dpad_pov(bool up, bool right, bool down, bool left)
{
    if (up && down) up = down = false;
    if (left && right) left = right = false;
    if (up)    return right ? 4500 : left ? 31500 : 0;
    if (down)  return right ? 13500 : left ? 22500 : 18000;
    if (right) return 9000;
    if (left)  return 27000;
    return -1;
}

/* @h's report @r (after its ID byte) into @st; false when it carries
 * nothing of the controller's */
static bool hp_decode(const HidPad *h, UINT8 id, const UINT8 *r, int len, PadState *st)
{
    bool any = false, dpad[4] = { false, false, false, false }, have_dpad = false, have_hat = false;
    for (int i = 0; i < h->n; i++) {
        const PadField *f = &h->f[i];
        if (f->id != id) continue;
        INT32 v = hp_bits(r, len, f->bit, f->size, f->lmin < 0);
        if (f->page == PAGE_BUTTON && f->usage >= 1 && f->usage <= 32) {
            if (v) st->buttons |= 1u << (f->usage - 1);
            else   st->buttons &= ~(1u << (f->usage - 1));
            any = true;
        } else if (f->page == PAGE_DESKTOP && f->usage >= 0x30 && f->usage <= 0x37) {
            INT64 lo = f->lmin, hi = f->lmax;
            if (hi <= lo) continue;
            if (v < lo) v = (INT32)lo;
            if (v > hi) v = (INT32)hi;
            st->axis[f->usage - 0x30] = (UINT16)(((INT64)v - lo) * 65535 / (hi - lo));
            any = true;
        } else if (f->page == PAGE_DESKTOP && f->usage == 0x39) {          /* Hat switch */
            INT32 positions = f->lmax - f->lmin + 1;
            st->pov = positions > 0 && v >= f->lmin && v <= f->lmax ? (INT32)((INT64)(v - f->lmin) * 36000 / positions) : -1;
            have_hat = any = true;
        } else if (f->page == PAGE_DESKTOP && f->usage >= 0x90 && f->usage <= 0x93) {   /* D-pad Up, Down, Right, Left */
            dpad[f->usage - 0x90] = v != 0;
            have_dpad = any = true;
        }
    }
    if (have_dpad && !have_hat) st->pov = dpad_pov(dpad[0], dpad[2], dpad[1], dpad[3]);
    return any;
}

void PadHidInput(void *pad, const UINT8 *data, int len)
{
    HidPad *h = pad;
    if (!h || len <= 0) return;
    UINT8 id = 0;
    if (h->ids) {
        id = data[0];
        data++; len--;
    }
    PadState st = h->last;
    if (hp_decode(h, id, data, len, &st) && memcmp(&st, &h->last, sizeof(st)) != 0) {
        h->last = st;
        PadReport(h->slot, &st);
    }
}

void PadHidClose(void *pad)
{
    HidPad *h = pad;
    if (!h) return;
    PadDetach(h->slot);
    kfree(h);
}

void *PadHidOpen(const UINT8 *desc, int len, UINT16 vid, UINT16 pid, const char *product, UINT32 node,
                 char *what, int cap)
{
    HidPad *h = kzalloc(sizeof(HidPad));
    if (!h) return NULL;
    UINT16 app = 0;
    hp_parse(h, desc, len, &app);
    if (app != 0x0104 && app != 0x0105) { kfree(h); return NULL; }   /* not a joystick or game pad */

    PadInfo info;
    memset(&info, 0, sizeof(info));
    info.kind = PAD_HID;
    info.vid = vid;
    info.pid = pid;
    info.usage = app;
    info.node = node;
    for (int i = 0; i < h->n; i++) {
        const PadField *pf = &h->f[i];
        if (pf->page == PAGE_BUTTON && pf->usage >= 1 && pf->usage <= 32 && pf->usage > info.buttons) info.buttons = (UINT8)pf->usage;
        if (pf->page == PAGE_DESKTOP && pf->usage >= 0x30 && pf->usage <= 0x37) info.axes |= (UINT8)(1u << (pf->usage - 0x30));
        if (pf->page == PAGE_DESKTOP && (pf->usage == 0x39 || (pf->usage >= 0x90 && pf->usage <= 0x93))) info.povs = 1;
    }
    if (product && product[0]) strncpy(info.name, product, sizeof(info.name) - 1);
    else ksnprintf(info.name, sizeof(info.name), app == 0x0104 ? "USB Joystick" : "USB Gamepad");

    h->last.pov = -1;
    for (int a = 0; a < PAD_AXES; a++) h->last.axis[a] = 32768;
    h->slot = PadAttach(&info, NULL, NULL);
    if (h->slot < 0) { kfree(h); return NULL; }
    ksnprintf(what, cap, "%s \"%s\" (%d buttons, %d axes%s)", app == 0x0104 ? "joystick" : "gamepad",
              info.name, info.buttons, __builtin_popcount(info.axes), info.povs ? ", a hat" : "");
    return h;
}
