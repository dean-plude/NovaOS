/*
 * hv_video.c — Hyper-V's synthetic video device
 *
 * A Generation 2 VM shows the firmware's frame buffer (the GOP one NovaOS
 * starts on) until a guest driver takes over the synthetic video device,
 * a VMBus channel; then the guest picks any mode the host lists.  The
 * protocol (synthvid, as Hyper-V's own guests speak it): each message is a
 * pipe header (1 = data, size) and a synthvid header (type, size of the
 * header and what follows), sent in-band:
 *
 *   VERSION_REQUEST -> VERSION_RESPONSE  3.5 (Windows 10), 3.2 (Windows 8),
 *                                        3.0: the first the host accepts
 *   RESOLUTION_REQUEST -> ..._RESPONSE   3.5 only: the modes the host offers
 *   VRAM_LOCATION -> VRAM_LOCATION_ACK   where the video memory is: the
 *                                        firmware's frame buffer, which is
 *                                        the start of the device's MMIO
 *                                        space (the offer says how many MiB)
 *   SITUATION_UPDATE                     the mode: width, height, 32 bpp,
 *                                        bytes a row, from offset 0
 *   POINTER_POSITION, POINTER_SHAPE      a 1x1 transparent pointer: NovaOS
 *                                        draws its own
 *   FEATURE_CHANGE (from the host)       whether it wants DIRT messages
 *   DIRT                                 which rectangles changed
 *
 * The host shows the video memory only where DIRT says it changed, so
 * while it wants them, the whole screen is reported every 50 ms (the
 * device poll thread's tick), as Linux's hyperv_fb does.  A mode change is
 * one SITUATION_UPDATE; the host does not answer it.
 */

#include "hv_video.h"
#include "../hal/display.h"
#include "../lib/string.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../mm/vmm.h"
#include "../arch/x86_64/cpu.h"
#include "../arch/x86_64/apic.h"

#define PIPE_DATA                  1
#define SV_VERSION_REQUEST         1
#define SV_VERSION_RESPONSE        2
#define SV_VRAM_LOCATION           3
#define SV_VRAM_LOCATION_ACK       4
#define SV_SITUATION_UPDATE        5
#define SV_POINTER_POSITION        7
#define SV_POINTER_SHAPE           8
#define SV_FEATURE_CHANGE          9
#define SV_DIRT                    10
#define SV_RESOLUTION_REQUEST      13
#define SV_RESOLUTION_RESPONSE     14

#define SV_VERSION(major, minor)   ((UINT32)(minor) << 16 | (major))
#define SV_WIN7                    SV_VERSION(3, 0)
#define SV_WIN8                    SV_VERSION(3, 2)
#define SV_WIN10                   SV_VERSION(3, 5)

#define REPLY_MS                   5000
#define DIRT_TICKS                 5     /* 50 ms */
#define MAX_RES                    64

typedef struct __attribute__((packed)) {
    UINT32 pipe_type, pipe_size;
    UINT32 type, size;                   /* size: this header and what follows */
} SvHeader;

typedef struct __attribute__((packed)) {
    UINT8  active;
    UINT32 vram_offset;
    UINT8  depth_bits;
    UINT32 width, height, pitch;
} SvOutput;

typedef struct __attribute__((packed)) {
    SvHeader h;
    union {
        struct { UINT32 version; UINT8 accepted, max_outputs; } version;
        struct { UINT8 max_count; } res_req;
        struct {
            UINT8 edid[128];
            UINT8 count, default_index, is_standard;
            struct { UINT16 w, h; } res[MAX_RES];
        } res;
        struct { UINT64 user_ctx; UINT8 gpa_specified; UINT64 gpa; } vram;
        struct { UINT64 user_ctx; } vram_ack;
        struct { UINT64 user_ctx; UINT8 count; SvOutput out; } situation;
        struct { UINT8 visible, output; INT32 x, y; } ptr_pos;
        struct { UINT8 part, argb; UINT32 w, h, hot_x, hot_y; UINT8 data[4]; } ptr_shape;
        struct { UINT8 dirt, ptr_pos, ptr_shape, situation; } feature;
        struct { UINT8 output, count; INT32 x1, y1, x2, y2; } dirt;
        UINT8 raw[512];
    };
} SvMsg;

_Static_assert(sizeof(SvOutput) == 18, "video_output_situation");

typedef struct {
    VmbusChannel *ch;
    volatile int  sending;               /* one writer on the outbound ring */
    volatile UINT32 waiting;             /* the reply type started() waits for */
    volatile bool got;
    SvMsg         reply;
    volatile bool dirt;                  /* the host wants DIRT messages */
    bool          attached;
    UINT32        version;
    UINT64        vram_phys, vram_size;
    int           w, h;                  /* the mode the host shows */
    UINT64        last_dirt;
    DisplayMode   modes[DISPLAY_MAX_MODES];
    int           nmodes;
} HvVideo;

static HvVideo *g_video;                 /* (display.c's set_mode has no context) */

static void take(volatile int *f)
{
    while (__atomic_exchange_n(f, 1, __ATOMIC_ACQUIRE)) {
        if (interrupts_enabled()) sched_yield();
        else pause_cpu();
    }
}
static void drop(volatile int *f) { __atomic_store_n(f, 0, __ATOMIC_RELEASE); }

static bool send(HvVideo *v, SvMsg *m, UINT32 type, UINT32 body)
{
    m->h.pipe_type = PIPE_DATA;
    m->h.type = type;
    m->h.size = 8 + body;
    m->h.pipe_size = m->h.size;
    take(&v->sending);
    bool ok = VmbusSend(v->ch, m, sizeof(SvHeader) + body, 0, false);
    drop(&v->sending);
    return ok;
}

/* Send @m and wait for a message of type @want (in v->reply) */
static bool ask(HvVideo *v, SvMsg *m, UINT32 type, UINT32 body, UINT32 want)
{
    v->got = false;
    __atomic_store_n(&v->waiting, want, __ATOMIC_RELEASE);
    bool ok = send(v, m, type, body);
    for (UINT32 t = 0; ok && !__atomic_load_n(&v->got, __ATOMIC_ACQUIRE); t++) {
        VmbusPollChannel(v->ch);
        if (__atomic_load_n(&v->got, __ATOMIC_ACQUIRE)) break;
        if (t >= REPLY_MS * 100) ok = false;
        else udelay(10);
    }
    __atomic_store_n(&v->waiting, 0, __ATOMIC_RELEASE);
    return ok && v->got;
}

static void video_packet(void *ctx, const UINT8 *d, UINT32 len, UINT64 trans_id)
{
    (void)trans_id;
    HvVideo *v = ctx;
    if (!v || len < sizeof(SvHeader)) return;
    const SvMsg *m = (const SvMsg *)d;
    if (m->h.pipe_type != PIPE_DATA) return;
    if (m->h.type == SV_FEATURE_CHANGE && len >= sizeof(SvHeader) + 4) {
        bool want = m->feature.dirt;
        if (want != v->dirt)
            kprintf("[HVVIDEO] The host %s which parts of the screen changed\n", want ? "wants to be told" : "no longer needs to know");
        __atomic_store_n(&v->dirt, want, __ATOMIC_RELEASE);
        return;
    }
    if (m->h.type == __atomic_load_n(&v->waiting, __ATOMIC_ACQUIRE) && !v->got) {
        memset(&v->reply, 0, sizeof(v->reply));
        memcpy(&v->reply, d, len < sizeof(v->reply) ? len : sizeof(v->reply));
        __atomic_store_n(&v->got, true, __ATOMIC_RELEASE);
    }
}

static bool situation(HvVideo *v, int w, int h)
{
    SvMsg m;
    memset(&m, 0, sizeof(m));
    m.situation.user_ctx = 0;
    m.situation.count = 1;
    m.situation.out.active = 1;
    m.situation.out.vram_offset = 0;
    m.situation.out.depth_bits = 32;
    m.situation.out.width = (UINT32)w;
    m.situation.out.height = (UINT32)h;
    m.situation.out.pitch = (UINT32)w * 4;
    if (!send(v, &m, SV_SITUATION_UPDATE, 9 + sizeof(SvOutput))) return false;
    v->w = w;
    v->h = h;
    return true;
}

static void dirt(HvVideo *v)
{
    SvMsg m;
    memset(&m, 0, sizeof(m));
    m.dirt.output = 0;
    m.dirt.count = 1;
    m.dirt.x2 = v->w;
    m.dirt.y2 = v->h;
    send(v, &m, SV_DIRT, 2 + 16);
}

static bool fits(const HvVideo *v, int w, int h)
{
    return w >= 640 && h >= 480 && (UINT64)w * h * 4 <= v->vram_size;
}

static void add_mode(HvVideo *v, int w, int h)
{
    if (!fits(v, w, h) || v->nmodes >= DISPLAY_MAX_MODES) return;
    for (int i = 0; i < v->nmodes; i++)
        if (v->modes[i].w == w && v->modes[i].h == h) return;
    v->modes[v->nmodes++] = (DisplayMode){ w, h };
}

static bool set_mode(int w, int h)
{
    HvVideo *v = g_video;
    if (!v || !fits(v, w, h) || !situation(v, w, h)) return false;
    dirt(v);
    return true;
}

static void video_tick(void *ctx)
{
    HvVideo *v = ctx;
    if (!v->attached || !__atomic_load_n(&v->dirt, __ATOMIC_ACQUIRE)) return;
    UINT64 now = sched_ticks();
    if (now - v->last_dirt < DIRT_TICKS) return;
    v->last_dirt = now;
    dirt(v);
}

static void *video_opened(VmbusChannel *ch)
{
    HvVideo *v = kzalloc(sizeof(HvVideo));
    if (!v) return NULL;
    v->ch = ch;
    v->dirt = true;                      /* (until the host says it needs none) */
    return v;
}

static void video_started(void *ctx)
{
    static const UINT32 versions[] = { SV_WIN10, SV_WIN8, SV_WIN7 };
    static const DisplayMode common[] = {   /* before Windows 10's list: what fits */
        { 2560, 1600 }, { 2560, 1440 }, { 1920, 1200 }, { 1920, 1080 }, { 1680, 1050 },
        { 1600, 1200 }, { 1600, 900 }, { 1440, 900 }, { 1366, 768 }, { 1280, 1024 },
        { 1280, 800 }, { 1280, 720 }, { 1152, 864 }, { 1024, 768 }, { 800, 600 }, { 640, 480 },
    };
    HvVideo *v = ctx;
    SvMsg m;
    for (UINT32 i = 0; i < sizeof(versions) / sizeof(versions[0]) && !v->version; i++) {
        memset(&m, 0, sizeof(m));
        m.version.version = versions[i];
        if (!ask(v, &m, SV_VERSION_REQUEST, 4, SV_VERSION_RESPONSE)) {
            kprintf("[HVVIDEO] No answer to the version request: the firmware's frame buffer stays\n");
            return;
        }
        if (v->reply.version.accepted) v->version = versions[i];
    }
    if (!v->version) {
        kprintf("[HVVIDEO] The host took no version NovaOS speaks (3.5, 3.2, 3.0)\n");
        return;
    }

    UINT64 base, size;
    int bw, bh;
    if (!DisplayBootFramebuffer(&base, &size, &bw, &bh)) {
        kprintf("[HVVIDEO] The screen is not the firmware's frame buffer: left alone\n");
        return;
    }
    v->vram_phys = base;
    v->vram_size = (UINT64)VmbusChannelMmioMegabytes(v->ch) << 20;
    if (v->vram_size < size) v->vram_size = size;

    if (v->version == SV_WIN10) {
        memset(&m, 0, sizeof(m));
        m.res_req.max_count = MAX_RES;
        if (ask(v, &m, SV_RESOLUTION_REQUEST, 1, SV_RESOLUTION_RESPONSE)) {
            int n = v->reply.res.count < MAX_RES ? v->reply.res.count : MAX_RES;
            for (int i = 0; i < n; i++) add_mode(v, v->reply.res.res[i].w, v->reply.res.res[i].h);
        }
    }
    if (!v->nmodes)
        for (UINT32 i = 0; i < sizeof(common) / sizeof(common[0]); i++) add_mode(v, common[i].w, common[i].h);
    add_mode(v, bw, bh);

    memset(&m, 0, sizeof(m));
    m.vram.user_ctx = base;
    m.vram.gpa_specified = 1;
    m.vram.gpa = base;
    if (!ask(v, &m, SV_VRAM_LOCATION, 17, SV_VRAM_LOCATION_ACK) || v->reply.vram_ack.user_ctx != base) {
        kprintf("[HVVIDEO] The host did not take the video memory at 0x%llx: the firmware's frame buffer stays\n",
                (unsigned long long)base);
        return;
    }
    if (!situation(v, bw, bh)) return;

    memset(&m, 0, sizeof(m));                            /* NovaOS draws the pointer itself */
    m.ptr_pos.visible = 1;
    send(v, &m, SV_POINTER_POSITION, 10);
    memset(&m, 0, sizeof(m));
    m.ptr_shape.part = 0xFF;                             /* (the whole shape in one part) */
    m.ptr_shape.argb = 1;
    m.ptr_shape.w = m.ptr_shape.h = 1;
    send(v, &m, SV_POINTER_SHAPE, 22);

    g_video = v;
    dirt(v);
    kprintf("[HVVIDEO] Synthetic video %u.%u: %llu MiB of video memory at 0x%llx, %d mode(s)\n",
            v->version & 0xFFFF, v->version >> 16, (unsigned long long)(v->vram_size >> 20),
            (unsigned long long)base, v->nmodes);
    v->attached = DisplayHyperVAttach(set_mode, v->modes, v->nmodes);
}

/* {da0a7802-e377-4aac-8e77-0558eb1073f8} */
/* The device was taken away: no more mode changes or dirt */
static void video_removed(void *ctx)
{
    HvVideo *v = ctx;
    v->attached = false;
    if (g_video == v) g_video = NULL;
}

const VmbusDriver HvVideoDriver = {
    { 0x02, 0x78, 0x0a, 0xda, 0x77, 0xe3, 0xac, 0x4a, 0x8e, 0x77, 0x05, 0x58, 0xeb, 0x10, 0x73, 0xf8 },
    "Hyper-V video", video_opened, video_packet, NULL, video_started, video_tick, NULL, video_removed,
};
