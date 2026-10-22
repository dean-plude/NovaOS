/*
 * display.h — display adapter driver: modes and page flipping
 *
 * The back ends:
 *   - Bochs/QEMU VBE "DISPI" (QEMU -vga std, qxl, virtio and vmware,
 *     -device bochs-display, VirtualBox's VBoxVGA): any resolution the
 *     adapter's video memory holds, set at run time, and two pages of
 *     video memory flipped by the display start offset, so a frame is
 *     never seen half written.
 *   - Cirrus Logic GD5446 (QEMU -vga cirrus): 800x600 and 640x480.
 *   - The UEFI GOP framebuffer the bootloader handed over: the boot mode
 *     only, no flipping (every other adapter).
 *   - Hyper-V's synthetic video (drivers/hv_video.c), which takes over the
 *     GOP framebuffer of a Generation 2 VM: the firmware's frame buffer is
 *     the start of its video memory, and any mode the host lists that fits
 *     there is set by a VMBus message (DisplayHyperVAttach, below).
 *   - A Windows display driver (WDDM, display-only) installed on the boot
 *     display takes over from the first or the last (DisplayWddmOffer,
 *     below): its modes, through NovaOS's dxgkrnl host.
 *
 * The driver keeps the framebuffer console (framebuffer.h) pointed at the
 * page on screen; the GDI reads its geometry from there.
 *
 * More monitors: each further Bochs/QEMU DISPI adapter that is not the boot
 * display (QEMU -device secondary-vga or bochs-display) is another head,
 * with its own modes, set through the same registers (BAR2 MMIO).  So is
 * each output of a virtio GPU with a monitor on it (drivers/virtio_gpu.c:
 * several monitors on one card); when the boot display is a virtio-vga
 * with more than one output, head 0 moves onto its output 1 too (a fourth
 * back end, a picture in memory the card shows).  Head 0 is the boot
 * display (the functions without "Head" in the name); the GDI composes one
 * desktop across all of them (gdi.h, GdiMonitor*).  Monitors on a virtio
 * GPU's outputs come and go while NovaOS runs (DisplayPoll).
 */

#pragma once

#include "../drivers/virtio_gpu.h"

#include "../include/types.h"
#include "../../include/boot_protocol.h"

typedef struct {
    int w, h;
} DisplayMode;

#define DISPLAY_MAX_MODES 32
#define DISPLAY_MAX_HEADS 4

/* Probe the adapter (after PciInitialize).  Never changes the mode. */
void DisplayInit(const BootFramebuffer *boot);

/* "Bochs VBE", "Cirrus Logic" or "UEFI GOP framebuffer" */
const char *DisplayDriverName(void);

/* The adapter the driver found ("QEMU QXL", "VMware SVGA II", ...);
 * NULL on the GOP framebuffer */
const char *DisplayAdapterName(void);

/* The modes the adapter can show, largest first; 32 bits per pixel. */
int  DisplayModeCount(void);
bool DisplayModeAt(int i, DisplayMode *out);
bool DisplayModeSupported(int w, int h);
DisplayMode DisplayCurrentMode(void);
DisplayMode DisplayBootMode(void);

/* The mode the user chose (Settings, or ChangeDisplaySettings with
 * CDS_UPDATEREGISTRY): what a temporary mode change returns to.  Starts
 * as the boot mode; DesktopSaveDisplayMode also keeps it in the registry
 * for the next boot. */
DisplayMode DisplayDefaultMode(void);
void DisplaySetDefaultMode(int w, int h);

/* Switch the hardware to @w x @h (a supported mode).  The framebuffer
 * console follows; the caller re-initialises whatever draws on it. */
bool DisplaySetMode(int w, int h);

/* Page flipping: when DisplayCanFlip(), DisplayBackPage() is the video
 * memory page that is not on screen (same geometry as the visible one),
 * and DisplayFlip() puts it on screen. */
bool    DisplayCanFlip(void);
UINT32 *DisplayBackPage(void);
void    DisplayFlip(void);

/* After S3: the adapter lost its mode while powered off; set the current
 * one again (video memory is not cleared; the caller redraws anyway, as
 * real hardware may not keep it).  Nothing on the GOP framebuffer, which
 * shows whatever the firmware's wake path set up. */
void DisplayResume(void);

/* -----------------------------------------------------------------------
 * Heads (monitors).  Head 0 is the boot display above; heads 1.. are the
 * further DISPI adapters, single-buffered (no page flipping).
 * ----------------------------------------------------------------------- */
int  DisplayHeadCount(void);              /* 1 + further heads; 0 without a display */
/* The adapter's name ("QEMU secondary-vga", ...; head 0: DisplayAdapterName
 * or the driver's name) */
const char *DisplayHeadName(int head);
int  DisplayHeadModeCount(int head);
bool DisplayHeadModeAt(int head, int i, DisplayMode *out);
bool DisplayHeadModeSupported(int head, int w, int h);
DisplayMode DisplayHeadMode(int head);
DisplayMode DisplayHeadDefaultMode(int head);
void DisplayHeadSetDefaultMode(int head, int w, int h);
bool DisplayHeadSetMode(int head, int w, int h);
/* Heads 1..: the visible video memory and its pixels per scanline (the
 * pixels are little-endian XRGB, as on head 0) */
UINT32 *DisplayHeadSurface(int head, int *stride);
/* A rectangle of head @head's screen (its pixels) was drawn: a virtio GPU
 * output copies it to the monitor; nothing on the others */
void DisplayHeadDamage(int head, int x, int y, int w, int h);
/* Direct presentation (um_gui.c): there is a 3D virtio GPU, and head
 * @head's pixels are memory it can write (the VGA framebuffer, both its
 * pages when flipping, a further DISPI adapter's video memory, or a virtio
 * GPU output's picture): where, in @s */
bool DisplayHeadScreen(int head, VgpuScreen *s);
/* That memory changed in a rectangle the display was not told of: show it */
void DisplayHeadShow(int head, int x, int y, int w, int h);
/* Called with each rectangle of a head's screen that was drawn, before it
 * is shown (the overlays go back over it) */
void DisplaySetDamageHook(void (*fn)(int head, int x, int y, int w, int h));
/* Called with the page about to be shown, before each flip */
void DisplaySetFlipHook(void (*fn)(int page));

/* Bytes moved to show frames, since boot (gltest fps compares the GDI way
 * of presenting with direct presentation) */
enum {
    DISPLAY_STAT_TO_HOST_2D,              /* guest memory to a virtio GPU: its outputs' pictures */
    DISPLAY_STAT_FROM_HOST,               /* a virtio GPU to guest memory: virgl's read-backs */
    DISPLAY_STAT_PAINTED,                 /* programs' window bitmaps drawn by the desktop */
    DISPLAY_STAT_SCREEN,                  /* the desktop's back buffer copied to the screens */
    DISPLAY_STAT_DIRECT_FRAMES,           /* frames shown by direct presentation */
    DISPLAY_STAT_DIRECT_BYTES,            /* ... and the bytes the host wrote into the screen for them */
    DISPLAY_STAT_DIRECT_US,               /* microseconds programs spent asking for them (in the kernel) */
    DISPLAY_STAT_DIRECT_LOCK_US,          /* ... of which waiting for the desktop */
    DISPLAY_STATS
};
void DisplayStatAdd(int k, UINT64 n);
void DisplayStats(UINT64 *out);
/* Desktop loop: whether monitors were plugged in or unplugged since the
 * last call.  A new one is the last head; *removed has bit N set for each
 * head N (by its number before the call) that went, the later heads
 * moving up a place each. */
bool DisplayPoll(UINT32 *removed);

/* -----------------------------------------------------------------------
 * A Windows display driver on the boot display (kernel/nt/dxgkrnl.c, the
 * WDDM host).  Its host offers it once the driver has started, with the
 * modes the driver's source mode set holds and the mode it shows; the
 * desktop loop hands it the screen (DisplayPoll says the display changed)
 * and from then on head 0 is a picture in memory whose drawn rectangles
 * go to @present, and DisplaySetMode asks @set_mode.  Withdraw (with the
 * desktop lock held, the driver no longer presenting) gives the screen
 * back to the built-in driver in the mode the driver left, @left.
 * ----------------------------------------------------------------------- */
typedef struct {
    const char *name;                     /* the driver's service ("WddmDod") */
    void       *ctx;
    bool (*set_mode)(void *ctx, int w, int h);
    void (*present)(void *ctx, const UINT32 *px, int stride, int x, int y, int w, int h);
} DisplayWddm;

void DisplayWddmOffer(const DisplayWddm *drv, const DisplayMode *modes, int n, DisplayMode cur);
void DisplayWddmWithdraw(const DisplayWddm *drv, DisplayMode left);
bool DisplayPostInfo(UINT64 *phys, int *w, int *h, int *pitch_bytes);

/* -----------------------------------------------------------------------
 * Hyper-V's synthetic video (drivers/hv_video.c), once the host took its
 * video memory: the GOP framebuffer becomes a display with @modes (@n),
 * each set by @set_mode, which shows w x h, 32 bpp, w * 4 bytes a row, from
 * the frame buffer's start.  False (nothing changes) when the screen is
 * not the GOP framebuffer.  DisplayBootFramebuffer: where it is.
 * ----------------------------------------------------------------------- */
bool DisplayHyperVAttach(bool (*set_mode)(int w, int h), const DisplayMode *modes, int n);
bool DisplayBootFramebuffer(UINT64 *phys, UINT64 *size, int *w, int *h);
/* The host would not put the video memory at the GOP framebuffer, so it
 * went to @phys (@size bytes, in the physmap) instead: the screen is drawn
 * there from now on, w * 4 bytes a row.  The desktop lays itself out again
 * when DisplayTakeMoved() says so (it reads the surface in its own loop) */
bool DisplayHyperVMove(UINT64 phys, UINT64 size);
bool DisplayTakeMoved(void);
