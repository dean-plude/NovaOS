/* Hyper-V's synthetic video device on the host (tests/tools/test_hv_video.py):
 * a fake host answers the driver's synthvid messages as Hyper-V would */
#include "../../kernel/drivers/hv_video.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- the kernel around the driver ---- */
#define FB_BASE 0xF8000000ull
static UINT32 mmio_mb = 16;
static UINT64 ticks = 100;
static bool   gop = true;
static int    attached, attach_n;
static bool (*attach_set)(int w, int h);
static DisplayMode attach_modes[DISPLAY_MAX_MODES];
static void (*shown_hook)(void);
static bool   aml_ready, fb_enabled = true;
static AmlRange ranges[AML_MAX_VMBUS_MMIO];
static int    nranges;
static UINT64 moved_to;
#define HIGH_MMIO 0xFE0000000ull                      /* (a Windows 11 host's: 63.5 GiB, 512 MiB) */

void kprintf(const char *f, ...) { va_list v; va_start(v, f); vprintf(f, v); va_end(v); }
void *kzalloc(size_t n) { return calloc(1, n); }
void udelay(UINT64 us) { (void)us; }
void sched_yield(void) {}
uint64_t sched_ticks(void) { return ticks; }
void fb_set_shown_hook(void (*shown)(void)) { shown_hook = shown; }
UINT32 VmbusChannelMmioMegabytes(const VmbusChannel *ch) { (void)ch; return mmio_mb; }
bool DisplayBootFramebuffer(UINT64 *phys, UINT64 *size, int *w, int *h)
{
    if (!gop) return false;
    *phys = FB_BASE; *size = 1024 * 768 * 4; *w = 1024; *h = 768;
    return true;
}
bool AmlReady(void) { return aml_ready; }
int AmlVmbusMmio(AmlRange *out, int max)
{
    int n = nranges < max ? nranges : max;
    memcpy(out, ranges, n * sizeof(*out));
    return n;
}
void kprintf_set_fb_enabled(bool enabled) { fb_enabled = enabled; }
bool DisplayHyperVMove(UINT64 phys, UINT64 size) { (void)size; moved_to = phys; return true; }
bool DisplayHyperVAttach(bool (*set_mode)(int w, int h), const DisplayMode *modes, int n)
{
    attached++;
    attach_set = set_mode;
    attach_n = n;
    memcpy(attach_modes, modes, n * sizeof(*modes));
    return true;
}

/* ---- the fake host ---- */
static UINT32 accepts;                      /* the one version it takes; 0: none */
static bool   silent, bad_ack;
static int    counts[16];                   /* messages by type */
static SvMsg  last[16];                     /* the last of each type */
static SvMsg  queued[4];
static void  *ctx;                         /* the driver's channel context */
static int    nqueued;
static const struct { UINT16 w, h; } host_res[] = {
    { 3840, 2160 }, { 1920, 1080 }, { 1280, 720 }, { 1024, 768 }, { 800, 600 }, { 320, 200 },
};

static void reply(UINT32 type, UINT32 body)
{
    SvMsg *m = &queued[nqueued++];
    m->h.pipe_type = PIPE_DATA;
    m->h.type = type;
    m->h.size = 8 + body;
    m->h.pipe_size = m->h.size;
}

static UINT32 body_len(UINT32 type)
{
    switch (type) {
    case SV_VERSION_REQUEST: return 4;
    case SV_RESOLUTION_REQUEST: return 1;
    case SV_VRAM_LOCATION: return 17;
    case SV_SITUATION_UPDATE: return 27;
    case SV_POINTER_POSITION: return 10;
    case SV_POINTER_SHAPE: return 22;
    case SV_DIRT: return 18;
    }
    assert(!"unexpected message");
    return 0;
}

bool VmbusSend(VmbusChannel *ch, const void *data, UINT32 len, UINT64 trans_id, bool want_completion)
{
    (void)ch;
    const SvMsg *m = data;
    assert(!trans_id && !want_completion && m->h.pipe_type == PIPE_DATA);
    assert(len == sizeof(SvHeader) + body_len(m->h.type));
    assert(m->h.size == len - 8 && m->h.pipe_size == m->h.size);
    counts[m->h.type]++;
    memset(&last[m->h.type], 0, sizeof(SvMsg));
    memcpy(&last[m->h.type], m, len);
    if (silent) return true;
    switch (m->h.type) {
    case SV_VERSION_REQUEST:
        reply(SV_VERSION_RESPONSE, 6);
        queued[nqueued - 1].version.version = m->version.version;
        queued[nqueued - 1].version.accepted = m->version.version == accepts;
        queued[nqueued - 1].version.max_outputs = 1;
        break;
    case SV_RESOLUTION_REQUEST: {
        assert(m->res_req.max_count == MAX_RES);
        reply(SV_RESOLUTION_RESPONSE, 131 + 4 * MAX_RES);
        SvMsg *r = &queued[nqueued - 1];
        r->res.count = sizeof(host_res) / sizeof(host_res[0]);
        for (int i = 0; i < r->res.count; i++) { r->res.res[i].w = host_res[i].w; r->res.res[i].h = host_res[i].h; }
        break;
    }
    case SV_VRAM_LOCATION:
        assert(m->vram.gpa_specified == 1 && m->vram.gpa == m->vram.user_ctx);
        reply(SV_VRAM_LOCATION_ACK, 8);                     /* (bad_ack: only the firmware's frame buffer refused) */
        queued[nqueued - 1].vram_ack.user_ctx = bad_ack && m->vram.gpa == FB_BASE ? 1 : m->vram.user_ctx;
        break;
    }
    return true;
}

void VmbusPollChannel(VmbusChannel *ch)
{
    (void)ch;
    for (int i = 0; i < nqueued; i++) HvVideoDriver.packet(ctx, (const UINT8 *)&queued[i], sizeof(SvHeader) + queued[i].h.size - 8, 0);
    nqueued = 0;
}

/* ---- the checks ---- */
static void fresh(UINT32 version)
{
    memset(counts, 0, sizeof(counts));
    accepts = version; silent = bad_ack = false; gop = true;
    attached = 0; nqueued = 0; g_video = g_console = NULL; shown_hook = NULL;
    aml_ready = false; fb_enabled = true; nranges = 0; moved_to = 0;
    ctx = HvVideoDriver.opened(NULL);
}

static bool has_mode(int w, int h)
{
    for (int i = 0; i < attach_n; i++)
        if (attach_modes[i].w == w && attach_modes[i].h == h) return true;
    return false;
}

static void windows10_checks(void)
{
    fresh(SV_WIN10);
    HvVideoDriver.started(ctx);
    HvVideo *v = ctx;
    assert(v->version == SV_WIN10 && counts[SV_VERSION_REQUEST] == 1 && counts[SV_RESOLUTION_REQUEST] == 1);
    assert(attached == 1 && v->attached && g_video == v && v->vram_size == 16u << 20);
    assert(has_mode(1920, 1080) && has_mode(1280, 720) && has_mode(1024, 768) && has_mode(800, 600));
    assert(!has_mode(3840, 2160) && !has_mode(320, 200) && attach_n == 4);   /* too big, too small */
    assert(last[SV_VRAM_LOCATION].vram.user_ctx == FB_BASE);
    SvOutput *o = &last[SV_SITUATION_UPDATE].situation.out;
    assert(last[SV_SITUATION_UPDATE].situation.count == 1 && o->active == 1 && o->depth_bits == 32);
    assert(o->width == 1024 && o->height == 768 && o->pitch == 4096 && !o->vram_offset);
    assert(counts[SV_POINTER_POSITION] == 1 && last[SV_POINTER_POSITION].ptr_pos.visible == 1);
    assert(counts[SV_POINTER_SHAPE] == 1 && last[SV_POINTER_SHAPE].ptr_shape.part == 0xFF);
    assert(last[SV_POINTER_SHAPE].ptr_shape.w == 1 && last[SV_POINTER_SHAPE].ptr_shape.h == 1);
    assert(counts[SV_DIRT] == 2 && last[SV_DIRT].dirt.count == 1 && last[SV_DIRT].dirt.x2 == 1024 && last[SV_DIRT].dirt.y2 == 768);
    puts("Windows 10 host passed");

    assert(shown_hook == console_shown);                    /* the boot log's lines are reported too ... */
    int c = counts[SV_DIRT];
    shown_hook();
    assert(counts[SV_DIRT] == c + 1 && last[SV_DIRT].dirt.x2 == 1024 && last[SV_DIRT].dirt.y2 == 768);
    v->sending = 1;                                         /* ... never waiting for another sender */
    shown_hook();
    assert(counts[SV_DIRT] == c + 1);
    v->sending = 0;
    puts("boot log passed");

    assert(attach_set(1920, 1080));
    o = &last[SV_SITUATION_UPDATE].situation.out;
    assert(o->width == 1920 && o->height == 1080 && o->pitch == 7680 && counts[SV_DIRT] == c + 2);
    assert(last[SV_DIRT].dirt.x2 == 1920 && last[SV_DIRT].dirt.y2 == 1080);
    int n = counts[SV_SITUATION_UPDATE];
    assert(!attach_set(3840, 2160) && !attach_set(320, 200) && counts[SV_SITUATION_UPDATE] == n);
    puts("mode changes passed");

    int d = counts[SV_DIRT];
    ticks = 1000;
    HvVideoDriver.tick(ctx);
    assert(counts[SV_DIRT] == d + 1);                       /* every 50 ms ... */
    ticks += DIRT_TICKS - 1;
    HvVideoDriver.tick(ctx);
    assert(counts[SV_DIRT] == d + 1);
    ticks += 1;
    HvVideoDriver.tick(ctx);
    assert(counts[SV_DIRT] == d + 2);
    SvMsg f;                                                /* ... while the host wants them */
    memset(&f, 0, sizeof(f));
    f.h.pipe_type = PIPE_DATA; f.h.type = SV_FEATURE_CHANGE; f.h.size = 12; f.h.pipe_size = 12;
    HvVideoDriver.packet(ctx, (const UINT8 *)&f, sizeof(SvHeader) + 4, 0);
    ticks += 100;
    HvVideoDriver.tick(ctx);
    assert(counts[SV_DIRT] == d + 2);
    f.feature.dirt = 1;
    HvVideoDriver.packet(ctx, (const UINT8 *)&f, sizeof(SvHeader) + 4, 0);
    HvVideoDriver.tick(ctx);
    assert(counts[SV_DIRT] == d + 3);
    puts("dirt passed");

    HvVideoDriver.removed(ctx);                             /* taken away: no more mode changes or dirt */
    d = counts[SV_DIRT];
    ticks += 100;
    HvVideoDriver.tick(ctx);
    assert(!g_video && !attach_set(1024, 768) && counts[SV_DIRT] == d && !shown_hook);
    puts("removal passed");
}

static void older_host_checks(void)
{
    mmio_mb = 8;                                            /* Windows 8's protocol: no mode list */
    fresh(SV_WIN8);
    HvVideoDriver.started(ctx);
    HvVideo *v = ctx;
    assert(v->version == SV_WIN8 && counts[SV_VERSION_REQUEST] == 2 && !counts[SV_RESOLUTION_REQUEST]);
    assert(last[SV_VERSION_REQUEST].version.version == SV_WIN8);
    assert(attached == 1 && has_mode(1920, 1080) && !has_mode(1920, 1200) && has_mode(640, 480));
    puts("Windows 8 host passed");

    fresh(SV_WIN7);
    HvVideoDriver.started(ctx);
    assert(((HvVideo *)ctx)->version == SV_WIN7 && counts[SV_VERSION_REQUEST] == 3 && attached == 1);
    puts("Windows 7 host passed");
    mmio_mb = 16;
}

static void refusal_checks(void)
{
    fresh(0);                                               /* no version in common */
    HvVideoDriver.started(ctx);
    assert(counts[SV_VERSION_REQUEST] == 3 && !counts[SV_VRAM_LOCATION] && !attached && !g_video);

    fresh(SV_WIN10);                                        /* a host that never answers */
    silent = true;
    HvVideoDriver.started(ctx);
    assert(counts[SV_VERSION_REQUEST] == 1 && !attached && !g_video && !shown_hook);

    fresh(SV_WIN10);                                        /* the video memory refused */
    bad_ack = true;                                         /* (asked twice) */
    HvVideoDriver.started(ctx);
    assert(counts[SV_VRAM_LOCATION] == 2 && !attached && !g_video);
    assert(counts[SV_SITUATION_UPDATE] == 1 && counts[SV_DIRT] == 2);   /* the boot mode still shown ... */
    assert(shown_hook == console_shown);
    shown_hook();
    ticks += 100;
    HvVideoDriver.tick(ctx);
    assert(counts[SV_DIRT] == 4);                          /* ... with the boot log and the tick */
    assert(!fb_enabled && counts[SV_VRAM_LOCATION] == 2);   /* the boot log off the screen; ACPI not up yet */
    ranges[0] = (AmlRange){ FB_BASE, 128ull << 20 };        /* then the VMBus ranges: the high one first */
    ranges[1] = (AmlRange){ HIGH_MMIO, 512ull << 20 };
    nranges = 2;
    aml_ready = true;
    HvVideoDriver.tick(ctx);
    assert(counts[SV_VRAM_LOCATION] == 3 && last[SV_VRAM_LOCATION].vram.gpa == HIGH_MMIO);
    assert(moved_to == HIGH_MMIO && attached == 1 && g_video == ctx && counts[SV_SITUATION_UPDATE] == 2);
    HvVideoDriver.tick(ctx);                                /* (once) */
    assert(counts[SV_VRAM_LOCATION] == 3);
    HvVideoDriver.removed(ctx);
    assert(!shown_hook);

    fresh(SV_WIN10);                                        /* only the low range, the frame buffer at its start */
    bad_ack = true;
    HvVideoDriver.started(ctx);
    ranges[0] = (AmlRange){ FB_BASE, 128ull << 20 };
    nranges = 1;
    aml_ready = true;
    HvVideoDriver.tick(ctx);
    assert(counts[SV_VRAM_LOCATION] == 3 && last[SV_VRAM_LOCATION].vram.gpa == FB_BASE + (16ull << 20));
    assert(moved_to == FB_BASE + (16ull << 20) && attached == 1);

    fresh(SV_WIN10);                                        /* no ranges: the screen stays */
    bad_ack = true;
    HvVideoDriver.started(ctx);
    aml_ready = true;
    HvVideoDriver.tick(ctx);
    assert(counts[SV_VRAM_LOCATION] == 2 && !moved_to && !attached);

    fresh(SV_WIN10);                                        /* another display driver owns the screen */
    gop = false;
    HvVideoDriver.started(ctx);
    assert(!counts[SV_VRAM_LOCATION] && !attached && !g_video && !shown_hook);
    HvVideoDriver.tick(ctx);
    assert(!counts[SV_DIRT]);
    puts("refusals passed");
}

int main(void)
{
    /* {da0a7802-e377-4aac-8e77-0558eb1073f8} */
    const UINT8 *g = HvVideoDriver.type;
    char s[40];
    snprintf(s, sizeof(s), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    assert(!strcmp(s, "da0a7802-e377-4aac-8e77-0558eb1073f8"));
    windows10_checks();
    older_host_checks();
    refusal_checks();
    puts("hv_video: all passed");
    return 0;
}
