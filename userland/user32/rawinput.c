/*
 * rawinput.c — Raw Input: the mouse, the keyboard and game controllers
 *
 * A program registers the top-level collections it wants (usage page and
 * usage: Generic Desktop 4 joysticks, 5 game pads, 8 multi-axis
 * controllers, or a whole page with RIDEV_PAGEONLY) and gets each input
 * report of a matching device as WM_INPUT: wParam RIM_INPUT while the
 * process is in the foreground, RIM_INPUTSINK otherwise (only with
 * RIDEV_INPUTSINK), lParam an HRAWINPUT that GetRawInputData reads (a
 * RAWINPUT of type RIM_TYPEHID, one report per message).  It goes to the
 * registration's hwndTarget, or without one to the foreground window's
 * focus.  RIDEV_DEVNOTIFY adds WM_INPUT_DEVICE_CHANGE (GIDC_ARRIVAL,
 * GIDC_REMOVAL, lParam the device handle) as devices come and go, and an
 * arrival for each matching device already there when registering.
 * GetRawInputBuffer takes the calling thread's WM_INPUT messages off its
 * queue into an array of RAWINPUT blocks.
 *
 * The Xbox controllers are the kernel's controller slots (novapad.h):
 * each has the HID report descriptor Windows' Xbox driver gives its HID
 * side ("IG_" in its name) and sends its reports through the kernel's
 * ring.  (A HID game pad or joystick is a HID class collection, below,
 * as on Windows; its slot, which the gamehid class driver feeds for
 * DirectInput, is not listed here.)  A
 * thread started at the first registration waits for reports and for
 * controllers to come or go, and posts the messages.  Device names are
 * the HID paths CreateFile opens (kernel/um/um_hid.c); RIDI_PREPARSEDDATA
 * is what HidD_GetPreparsedData returns, for hid.dll's HidP_* calls.
 *
 * The mouse (Generic Desktop usage 2) and the keyboard (6) are one device
 * each, every mouse and keyboard together, as Windows' Raw Input shows
 * them on a machine with one of each: WM_INPUT carries a RAWMOUSE
 * (relative motion, the buttons that went down or up, the wheels' turns)
 * or a RAWKEYBOARD (scan code, break and E0 flags, virtual key, the
 * message the key makes) as the desktop takes them from the input queue,
 * through the same ring as the controllers' reports (slots
 * NOVA_RAW_MOUSE and NOVA_RAW_KEYBOARD).  RIDEV_NOLEGACY (flags 0x30,
 * which share their bits with RIDEV_EXCLUDE 0x10 and RIDEV_PAGEONLY 0x20:
 * the three are a mode) stops the process's legacy mouse or keyboard
 * messages (msg.c asks raw_nolegacy).
 *
 * The HID collections the kernel's HID class driver runs (kernel/nt/
 * hidclass.c: a USB HID device on its HidUsb minidriver, or a vendor's
 * minidriver) are Raw Input's devices too, as win32k makes them on
 * Windows: each device node HID\... whose driver turned the HID interface
 * on is a RIM_TYPEHID device, its name the interface path, its
 * RIDI_DEVICEINFO and RIDI_PREPARSEDDATA what IOCTL_HID_GET_COLLECTION_
 * INFORMATION and _DESCRIPTOR say; keyboard and mouse collections (on the
 * kbdhid and mouhid class drivers) are the keyboard and the mouse instead,
 * as on Windows.  While the process has registrations a
 * thread per collection holds a handle of its own (the class driver queues
 * every input report for each open handle) and posts each report ReadFile
 * gives as WM_INPUT; the reader looks the device tree over again when
 * Plug and Play reports a change, and announces arrivals and removals.
 */
#include "u32.h"
#include <novapad.h>
#include <novahidp.h>
#include <novapnp.h>

WINBASEAPI BOOL WINAPI DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD in_n, LPVOID out, DWORD out_n,
                                       LPDWORD ret, LPOVERLAPPED ov);

#define MAX_REG   32
#define HELD      256                 /* raw input handles kept for GetRawInputData */
#define DEV_BASE  0x10000             /* device handles: DEV_BASE + serial * 8 + slot */
#define DEV_MOUSE    ((HANDLE)(ULONG_PTR)(DEV_BASE + 1))
#define DEV_KEYBOARD ((HANDLE)(ULONG_PTR)(DEV_BASE + 2))
#define USAGE_MOUSE    0x00010002     /* usage page << 16 | usage */
#define USAGE_KEYBOARD 0x00010006
#define MODE(f)   ((f) & 0x30)        /* RIDEV_EXCLUDE, RIDEV_PAGEONLY, RIDEV_NOLEGACY */
#define REPORT_MAX 256                /* longest input report kept for WM_INPUT */
#define MAX_CLASS  16                 /* class-driver collections */
#define DEV_CLASS  0x20000000         /* their device handles: DEV_CLASS + a number never reused */

typedef struct {
    DWORD id;                         /* HRAWINPUT value (0: free) */
    DWORD type;                       /* RIM_TYPEMOUSE, RIM_TYPEKEYBOARD, RIM_TYPEHID */
    HANDLE dev;
    WPARAM code;
    DWORD len;
    union { BYTE data[REPORT_MAX]; RAWMOUSE mouse; RAWKEYBOARD keyboard; };
} Held;

static SRWLOCK        g_lock = SRWLOCK_INIT;
static RAWINPUTDEVICE g_reg[MAX_REG];
static int            g_nreg;
static Held           g_held[HELD];
static DWORD          g_next_id;
static LONG           g_thread;       /* the reader has started */
static DWORD          g_known[NOVA_PAD_SLOTS];   /* serials the reader has announced */

/* ---------------------------------------------------------------------------
 * Devices
 * ------------------------------------------------------------------------- */

/* Slot @slot's controller when Raw Input reads it from the slot: an Xbox
 * controller's HID side.  (A HID controller is its HID collection's, a
 * class device below, as on Windows.) */
static BOOL pad_info(int slot, NovaPadInfo *i)
{
    return nova_pad_info(slot, i) && !i->node;
}

static HANDLE dev_handle(int slot, DWORD serial) { return (HANDLE)(ULONG_PTR)(DEV_BASE + serial * 8 + (DWORD)slot); }

/* The controller a device handle names (its slot), or -1 */
static int dev_slot(HANDLE h, NovaPadInfo *info)
{
    ULONG_PTR v = (ULONG_PTR)h;
    if (v < DEV_BASE + 8) return -1;
    int slot = (int)(v & 7);
    DWORD serial = (DWORD)((v - DEV_BASE) >> 3);
    NovaPadInfo i;
    if (!pad_info(slot, &i) || i.serial != serial) return -1;
    if (info) *info = i;
    return slot;
}

/* A controller's top-level collection: usage page << 16 | usage */
static DWORD dev_usage(const NovaPadInfo *i)
{
    return (DWORD)(i->usage >> 8) << 16 | (i->usage & 0xFF);
}

/* ---------------------------------------------------------------------------
 * The HID class driver's collections (under g_class_lock)
 * ------------------------------------------------------------------------- */

/* The HID class driver's requests (hidclass.h) */
#define HID_CTL(fn, method)         ((0x0B << 16) | ((fn) << 2) | (method))
#define IOCTL_COLLECTION_INFO       HID_CTL(106, 0)
#define IOCTL_COLLECTION_DESC       HID_CTL(100, 3)

typedef struct {                       /* HID_COLLECTION_INFORMATION */
    ULONG  DescriptorSize;
    BOOLEAN Polled;
    UCHAR  Reserved1[1];
    USHORT VendorID, ProductID, VersionNumber;
} CollectionInfo;

typedef struct {
    HANDLE dev;                        /* its Raw Input handle (NULL: a free entry) */
    DWORD  node;                       /* its device node */
    WCHAR  path[200];                  /* the HID interface path */
    USHORT vid, pid, version;
    DWORD  usage;                      /* usage page << 16 | usage */
    UINT   in_len;                     /* input report length (ID byte included) */
    BYTE  *pp;                         /* preparsed data */
    UINT   pp_len;
    BOOL   reading;                    /* its reading thread runs */
} ClassDev;

static SRWLOCK  g_class_lock = SRWLOCK_INIT;
static ClassDev g_class[MAX_CLASS];
static DWORD    g_class_next;

/* The entry of class device handle @h (under g_class_lock), or NULL */
static ClassDev *class_dev(HANDLE h)
{
    if (!h) return NULL;
    for (int i = 0; i < MAX_CLASS; i++) if (g_class[i].dev == h) return &g_class[i];
    return NULL;
}

/* Whether node @n is a collection the HID class driver runs: HID\..., there,
 * its HID interface turned on by its driver.  A keyboard or a mouse (on
 * kbdhid or mouhid) is not: its keys and motion are the keyboard's and the
 * mouse's (RIM_TYPEKEYBOARD, RIM_TYPEMOUSE), as on Windows. */
static BOOL class_node(const NovaPnpNode *n)
{
    static const GUID hid = NOVA_IF_HID;
    if (!nova_pnp_prefix(n->instance, "HID\\") || !(n->flags & NOVA_PNP_PRESENT)) return FALSE;
    if (!lstrcmpiA(n->cls, "Keyboard") || !lstrcmpiA(n->cls, "Mouse")) return FALSE;
    GUID w[8];
    int m = (int)NtNovaGuiCtl(0, NOVA_CTL_PNP, 3 | (ULONG_PTR)n->id << 8, w);
    for (int j = 0; j < m && j < 8; j++) if (!memcmp(&w[j], &hid, sizeof(GUID))) return TRUE;
    return FALSE;
}

/* What the collection at @path is (into @d): its attributes and preparsed data */
static BOOL class_query(ClassDev *d)
{
    HANDLE h = CreateFileW(d->path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    CollectionInfo ci;
    DWORD n = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_COLLECTION_INFO, NULL, 0, &ci, sizeof(ci), &n, NULL) && ci.DescriptorSize;
    BYTE *pp = ok ? HeapAlloc(GetProcessHeap(), 0, ci.DescriptorSize) : NULL;
    ok = pp && DeviceIoControl(h, IOCTL_COLLECTION_DESC, NULL, 0, pp, ci.DescriptorSize, &n, NULL);
    CloseHandle(h);
    const NovaHidP *hp = ok ? nova_hidp_check(pp) : NULL;
    if (!hp) { if (pp) HeapFree(GetProcessHeap(), 0, pp); return FALSE; }
    d->vid = ci.VendorID;
    d->pid = ci.ProductID;
    d->version = ci.VersionNumber;
    d->usage = (DWORD)hp->page << 16 | hp->usage;
    d->in_len = hp->rlen[0];
    d->pp = pp;
    d->pp_len = ci.DescriptorSize;
    return TRUE;
}

static void notify(DWORD usage, HANDLE dev, BOOL arrived);
static void class_start(ClassDev *d);

/* Look the device tree over: collections that came get an entry (and,
 * once the reader runs, an announcement and a reading thread), those that
 * went lose theirs */
static void class_refresh(void)
{
    struct { DWORD usage; HANDLE dev; BOOL arrived; } ev[2 * MAX_CLASS];
    int nev = 0;
    BOOL seen[MAX_CLASS] = { 0 };
    static const GUID hid = NOVA_IF_HID;
    DWORD count = nova_pnp_count();
    NovaPnpNode node;
    AcquireSRWLockExclusive(&g_class_lock);
    for (DWORD id = 1; id <= count; id++) {
        if (!nova_pnp_node(id, &node) || !class_node(&node)) continue;
        int at = -1, free_at = -1;
        for (int i = 0; i < MAX_CLASS; i++) {
            if (g_class[i].dev && g_class[i].node == id) at = i;
            else if (!g_class[i].dev && free_at < 0) free_at = i;
        }
        if (at >= 0) {
            seen[at] = TRUE;
            if (g_thread) class_start(&g_class[at]);   /* (its thread gave up: again) */
            continue;
        }
        if (free_at < 0) continue;
        ClassDev *d = &g_class[free_at];
        memset(d, 0, sizeof(*d));
        d->node = id;
        nova_pnp_iface_path(&node, &hid, d->path, 200);
        if (!class_query(d)) continue;                 /* (not ready yet: the next look) */
        d->dev = (HANDLE)(ULONG_PTR)(DEV_CLASS + ++g_class_next);
        seen[free_at] = TRUE;
        if (g_thread) {
            ev[nev].usage = d->usage; ev[nev].dev = d->dev; ev[nev++].arrived = TRUE;
            class_start(d);
        }
    }
    for (int i = 0; i < MAX_CLASS; i++) {
        ClassDev *d = &g_class[i];
        if (!d->dev || seen[i]) continue;
        if (g_thread) { ev[nev].usage = d->usage; ev[nev].dev = d->dev; ev[nev++].arrived = FALSE; }
        HeapFree(GetProcessHeap(), 0, d->pp);
        d->dev = NULL;                                 /* (its reading thread sees it go) */
        d->pp = NULL;
    }
    ReleaseSRWLockExclusive(&g_class_lock);
    for (int i = 0; i < nev; i++) notify(ev[i].usage, ev[i].dev, ev[i].arrived);
}

/* ---------------------------------------------------------------------------
 * Registrations (under g_lock)
 * ------------------------------------------------------------------------- */

/* The registration that takes devices of @usage (page << 16 | usage), or NULL */
static RAWINPUTDEVICE *reg_for(DWORD usage)
{
    USHORT page = (USHORT)(usage >> 16), u = (USHORT)usage;
    RAWINPUTDEVICE *page_only = NULL;
    for (int i = 0; i < g_nreg; i++) {
        RAWINPUTDEVICE *r = &g_reg[i];
        if (r->usUsagePage != page) continue;
        if (r->usUsage == u && MODE(r->dwFlags) != RIDEV_PAGEONLY)
            return MODE(r->dwFlags) == RIDEV_EXCLUDE ? NULL : r;
        if (MODE(r->dwFlags) == RIDEV_PAGEONLY && !page_only) page_only = r;
    }
    return page_only;
}

/* Where input for registration @r goes now (*@code RIM_INPUT or
 * RIM_INPUTSINK), or NULL when it is not wanted */
static HWND target_of(const RAWINPUTDEVICE *r, WPARAM *code)
{
    HWND fg = GetForegroundWindow();                /* (this process's, if it is in front) */
    if (r->hwndTarget) {
        if (!IsWindow(r->hwndTarget)) return NULL;
        if (fg) { *code = RIM_INPUT; return r->hwndTarget; }
        if (r->dwFlags & (RIDEV_INPUTSINK | RIDEV_EXINPUTSINK)) { *code = RIM_INPUTSINK; return r->hwndTarget; }
        return NULL;
    }
    if (!fg) return NULL;
    GUITHREADINFO gi;
    memset(&gi, 0, sizeof(gi));
    gi.cbSize = sizeof(gi);
    *code = RIM_INPUT;
    if (GetGUIThreadInfo(GetWindowThreadProcessId(fg, NULL), &gi) && gi.hwndFocus) return gi.hwndFocus;
    return fg;
}

/* ---------------------------------------------------------------------------
 * The reader
 * ------------------------------------------------------------------------- */

/* What a mouse or keyboard block of the ring is as RAWMOUSE or
 * RAWKEYBOARD (into @h) */
static void from_ring(const NovaPadRaw *r, Held *h)
{
    if (r->slot == NOVA_RAW_MOUSE) {
        NovaRawMouse m;
        memcpy(&m, r->data, sizeof(m));
        memset(&h->mouse, 0, sizeof(h->mouse));
        h->mouse.usFlags = m.flags;
        h->mouse.usButtonFlags = m.button_flags;
        h->mouse.usButtonData = (USHORT)m.button_data;
        h->mouse.ulRawButtons = m.raw_buttons;
        h->mouse.lLastX = m.x;
        h->mouse.lLastY = m.y;
        h->type = RIM_TYPEMOUSE;
        h->dev = DEV_MOUSE;
        h->len = sizeof(RAWMOUSE);
    } else {
        NovaRawKey k;
        memcpy(&k, r->data, sizeof(k));
        memset(&h->keyboard, 0, sizeof(h->keyboard));
        h->keyboard.MakeCode = k.make;
        h->keyboard.Flags = k.flags;
        h->keyboard.VKey = k.vkey;
        h->keyboard.Message = k.message;
        h->type = RIM_TYPEKEYBOARD;
        h->dev = DEV_KEYBOARD;
        h->len = sizeof(RAWKEYBOARD);
    }
}

/* An input report of class device @dev (whose collection is @usage) */
static void deliver_class(HANDLE dev, DWORD usage, const BYTE *data, UINT len)
{
    if (len > REPORT_MAX) len = REPORT_MAX;
    AcquireSRWLockExclusive(&g_lock);
    RAWINPUTDEVICE *reg = reg_for(usage);
    WPARAM code = 0;
    HWND to = reg ? target_of(reg, &code) : NULL;
    DWORD id = 0;
    if (to) {
        if (!++g_next_id) g_next_id = 1;
        id = g_next_id;
        Held *h = &g_held[id % HELD];
        h->id = id;
        h->code = code;
        h->type = RIM_TYPEHID;
        h->dev = dev;
        h->len = len;
        memcpy(h->data, data, len);
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (to) PostMessageW(to, WM_INPUT, code, (LPARAM)id);
}

/* A class device's reading thread: its own handle, a report per ReadFile,
 * until the device goes */
static DWORD WINAPI class_reader(LPVOID arg)
{
    HANDLE dev = arg, h = INVALID_HANDLE_VALUE;
    WCHAR path[200];
    DWORD usage = 0;
    UINT len = 0;
    AcquireSRWLockShared(&g_class_lock);
    ClassDev *d = class_dev(dev);
    if (d) { lstrcpynW(path, d->path, 200); usage = d->usage; len = d->in_len; }
    ReleaseSRWLockShared(&g_class_lock);
    if (d && len) h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    BYTE *buf = h != INVALID_HANDLE_VALUE ? HeapAlloc(GetProcessHeap(), 0, len) : NULL;
    for (int fails = 0; buf && fails < 50;) {
        DWORD n = 0;
        if (ReadFile(h, buf, len, &n, NULL) && n) { fails = 0; deliver_class(dev, usage, buf, n); continue; }
        AcquireSRWLockShared(&g_class_lock);           /* (gone: stop; else try again) */
        BOOL there = class_dev(dev) != NULL;
        ReleaseSRWLockShared(&g_class_lock);
        if (!there) break;
        fails++;
        Sleep(100);
    }
    if (buf) HeapFree(GetProcessHeap(), 0, buf);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    AcquireSRWLockExclusive(&g_class_lock);
    if ((d = class_dev(dev))) d->reading = FALSE;
    ReleaseSRWLockExclusive(&g_class_lock);
    return 0;
}

/* Start @d's reading thread (under g_class_lock) */
static void class_start(ClassDev *d)
{
    if (d->reading) return;
    HANDLE t = CreateThread(NULL, 0, class_reader, d->dev, 0, NULL);
    if (!t) return;
    d->reading = TRUE;
    CloseHandle(t);
}

static void deliver(const NovaPadRaw *r)
{
    NovaPadInfo info;
    DWORD usage;
    if (r->slot == NOVA_RAW_MOUSE) usage = USAGE_MOUSE;
    else if (r->slot == NOVA_RAW_KEYBOARD) usage = USAGE_KEYBOARD;
    else if (!pad_info(r->slot, &info) || info.serial != r->serial) return;
    else usage = dev_usage(&info);
    AcquireSRWLockExclusive(&g_lock);
    RAWINPUTDEVICE *reg = reg_for(usage);
    WPARAM code = 0;
    HWND to = reg ? target_of(reg, &code) : NULL;
    DWORD id = 0;
    if (to) {
        if (!++g_next_id) g_next_id = 1;
        id = g_next_id;
        Held *h = &g_held[id % HELD];
        h->id = id;
        h->code = code;
        if (r->slot >= NOVA_PAD_SLOTS) from_ring(r, h);
        else {
            h->type = RIM_TYPEHID;
            h->dev = dev_handle(r->slot, r->serial);
            h->len = r->len;
            memcpy(h->data, r->data, r->len);
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (to) PostMessageW(to, WM_INPUT, code, (LPARAM)id);
}

/* A controller came (@arrived) or went: to every registration with
 * RIDEV_DEVNOTIFY that takes it */
static void notify(DWORD usage, HANDLE dev, BOOL arrived)
{
    AcquireSRWLockShared(&g_lock);
    RAWINPUTDEVICE *reg = reg_for(usage);
    WPARAM code;
    HWND to = reg && (reg->dwFlags & RIDEV_DEVNOTIFY) ? reg->hwndTarget ? reg->hwndTarget : target_of(reg, &code) : NULL;
    ReleaseSRWLockShared(&g_lock);
    if (to) PostMessageW(to, WM_INPUT_DEVICE_CHANGE, arrived ? GIDC_ARRIVAL : GIDC_REMOVAL, (LPARAM)dev);
}

static NovaPadInfo g_known_info[NOVA_PAD_SLOTS];

/* Announce the controllers that came and went since the last look */
static void changes(void)
{
    for (int s = 0; s < NOVA_PAD_SLOTS; s++) {
        NovaPadInfo i;
        DWORD serial = pad_info(s, &i) ? i.serial : 0;
        if (serial == g_known[s]) continue;
        if (g_known[s]) notify(dev_usage(&g_known_info[s]), dev_handle(s, g_known[s]), FALSE);
        g_known[s] = serial;
        if (serial) { g_known_info[s] = i; notify(dev_usage(&i), dev_handle(s, serial), TRUE); }
    }
}

static DWORD WINAPI reader(LPVOID arg)
{
    (void)arg;
    static NovaPadRead q;
    memset(&q, 0, sizeof(q));
    nova_pad_read(&q);                                 /* (max 0: where the ring is now) */
    DWORD after = q.newest, known = q.changes;
    NovaPnpRead pr;                                    /* Plug and Play's changes, for the class devices */
    memset(&pr, 0, sizeof(pr));
    nova_pnp_events(&pr);                              /* (max 0: just the newest) */
    DWORD pnp_after = pr.newest;
    int looks = 0;
    for (;;) {
        q.after = after;
        q.known_changes = known;
        q.wait_ms = 250;
        q.max = 32;
        int n = nova_pad_read(&q);
        if (q.changes != known) { known = q.changes; changes(); }
        pr.after = pnp_after;
        pr.wait_ms = 0;
        pr.max = 32;
        if (nova_pnp_events(&pr) > 0 || pr.newest != pnp_after) {
            pnp_after = pr.newest;
            looks = 8;                                 /* (a driver turns the interface on after the
                                                          node arrives: look for 2 s) */
        }
        if (looks) { looks--; class_refresh(); }
        for (int i = 0; i < n; i++) deliver(&q.r[i]);
        after = n ? q.r[n - 1].seq : q.newest > after ? q.newest : after;
    }
    return 0;
}

static void start_reader(void)
{
    class_refresh();                                   /* (before the reader: no announcements) */
    if (InterlockedCompareExchange(&g_thread, 1, 0)) return;
    AcquireSRWLockExclusive(&g_class_lock);           /* a reading thread for each class device */
    for (int i = 0; i < MAX_CLASS; i++) if (g_class[i].dev) class_start(&g_class[i]);
    ReleaseSRWLockExclusive(&g_class_lock);
    for (int s = 0; s < NOVA_PAD_SLOTS; s++) {         /* what is there now counts as announced */
        NovaPadInfo i;
        g_known[s] = pad_info(s, &i) ? i.serial : 0;
        if (g_known[s]) g_known_info[s] = i;
    }
    HANDLE t = CreateThread(NULL, 0, reader, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

/* ---------------------------------------------------------------------------
 * The API
 * ------------------------------------------------------------------------- */

USERAPI BOOL RegisterRawInputDevices(PCRAWINPUTDEVICE d, UINT n, UINT cb)
{
    if (!d || !n || cb != sizeof(RAWINPUTDEVICE)) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    for (UINT i = 0; i < n; i++) {
        if ((d[i].dwFlags & RIDEV_REMOVE) && d[i].hwndTarget) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
        if ((d[i].dwFlags & (RIDEV_INPUTSINK | RIDEV_EXINPUTSINK)) && !d[i].hwndTarget) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
        if (d[i].hwndTarget && !IsWindow(d[i].hwndTarget)) { SetLastError(ERROR_INVALID_WINDOW_HANDLE); return FALSE; }
    }
    BOOL added = FALSE;
    AcquireSRWLockExclusive(&g_lock);
    for (UINT i = 0; i < n; i++) {
        int at = -1;
        for (int k = 0; k < g_nreg; k++)
            if (g_reg[k].usUsagePage == d[i].usUsagePage && g_reg[k].usUsage == d[i].usUsage) at = k;
        if (d[i].dwFlags & RIDEV_REMOVE) {
            if (at >= 0) g_reg[at] = g_reg[--g_nreg];
            continue;
        }
        if (at < 0) {
            if (g_nreg == MAX_REG) { ReleaseSRWLockExclusive(&g_lock); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
            at = g_nreg++;
        }
        g_reg[at] = d[i];
        added = TRUE;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!added) return TRUE;
    start_reader();
    for (UINT i = 0; i < n; i++) {                     /* the devices already there */
        if (!(d[i].dwFlags & RIDEV_DEVNOTIFY) || (d[i].dwFlags & RIDEV_REMOVE) || MODE(d[i].dwFlags) == RIDEV_EXCLUDE) continue;
        BOOL page_only = MODE(d[i].dwFlags) == RIDEV_PAGEONLY;
        if (d[i].usUsagePage == 1 && (page_only || d[i].usUsage == 2)) notify(USAGE_MOUSE, DEV_MOUSE, TRUE);
        if (d[i].usUsagePage == 1 && (page_only || d[i].usUsage == 6)) notify(USAGE_KEYBOARD, DEV_KEYBOARD, TRUE);
        for (int s = 0; s < NOVA_PAD_SLOTS; s++) {
            NovaPadInfo info;
            if (!pad_info(s, &info)) continue;
            DWORD u = dev_usage(&info);
            if ((USHORT)(u >> 16) != d[i].usUsagePage) continue;
            if (!page_only && (USHORT)u != d[i].usUsage) continue;
            notify(u, dev_handle(s, info.serial), TRUE);
        }
        struct { DWORD usage; HANDLE dev; } cd[MAX_CLASS];
        int ncd = 0;
        AcquireSRWLockShared(&g_class_lock);
        for (int k = 0; k < MAX_CLASS; k++) {
            ClassDev *c = &g_class[k];
            if (!c->dev || (USHORT)(c->usage >> 16) != d[i].usUsagePage) continue;
            if (!page_only && (USHORT)c->usage != d[i].usUsage) continue;
            cd[ncd].usage = c->usage;
            cd[ncd++].dev = c->dev;
        }
        ReleaseSRWLockShared(&g_class_lock);
        for (int k = 0; k < ncd; k++) notify(cd[k].usage, cd[k].dev, TRUE);
    }
    return TRUE;
}

USERAPI UINT GetRegisteredRawInputDevices(PRAWINPUTDEVICE d, PUINT n, UINT cb)
{
    if (!n || cb != sizeof(RAWINPUTDEVICE)) { SetLastError(ERROR_INVALID_PARAMETER); return (UINT)-1; }
    AcquireSRWLockShared(&g_lock);
    UINT have = (UINT)g_nreg;
    if (!d) { *n = have; ReleaseSRWLockShared(&g_lock); return 0; }
    if (*n < have) { *n = have; ReleaseSRWLockShared(&g_lock); SetLastError(ERROR_INSUFFICIENT_BUFFER); return (UINT)-1; }
    memcpy(d, g_reg, have * sizeof(*d));
    ReleaseSRWLockShared(&g_lock);
    return have;
}

USERAPI UINT GetRawInputDeviceList(PRAWINPUTDEVICELIST list, PUINT n, UINT cb)
{
    if (!n || cb != sizeof(RAWINPUTDEVICELIST)) { SetLastError(ERROR_INVALID_PARAMETER); return (UINT)-1; }
    RAWINPUTDEVICELIST all[2 + NOVA_PAD_SLOTS + MAX_CLASS];
    UINT have = 0;
    all[have].hDevice = DEV_MOUSE;
    all[have++].dwType = RIM_TYPEMOUSE;
    all[have].hDevice = DEV_KEYBOARD;
    all[have++].dwType = RIM_TYPEKEYBOARD;
    for (int s = 0; s < NOVA_PAD_SLOTS; s++) {
        NovaPadInfo i;
        if (!pad_info(s, &i)) continue;
        all[have].hDevice = dev_handle(s, i.serial);
        all[have].dwType = RIM_TYPEHID;
        have++;
    }
    if (!g_thread) class_refresh();                    /* (the reader keeps it current once it runs) */
    AcquireSRWLockShared(&g_class_lock);
    for (int i = 0; i < MAX_CLASS; i++) {
        if (!g_class[i].dev) continue;
        all[have].hDevice = g_class[i].dev;
        all[have++].dwType = RIM_TYPEHID;
    }
    ReleaseSRWLockShared(&g_class_lock);
    if (!list) { *n = have; return 0; }
    if (*n < have) { *n = have; SetLastError(ERROR_INSUFFICIENT_BUFFER); return (UINT)-1; }
    memcpy(list, all, have * sizeof(*list));
    return have;
}

/* The mouse's or the keyboard's: names as Windows gives a PS/2 one
 * (GUID_DEVINTERFACE_MOUSE, GUID_DEVINTERFACE_KEYBOARD) */
static UINT input_device_info(BOOL mouse, UINT cmd, LPVOID data, PUINT size, BOOL wide)
{
    switch (cmd) {
    case RIDI_DEVICENAME: {
        const char *name = mouse ? "\\\\?\\ACPI#PNP0F13#4&1d5e3c1b&0#{378de44c-56ef-11d1-bc8c-00a0c91405dd}"
                                 : "\\\\?\\ACPI#PNP0303#4&1d5e3c1b&0#{884b96c3-56ef-11d1-bc8c-00a0c91405dd}";
        UINT len = (UINT)strlen(name) + 1;
        if (!data) { *size = len; return 0; }
        if (*size < len) { *size = len; SetLastError(ERROR_INSUFFICIENT_BUFFER); return (UINT)-1; }
        for (UINT i = 0; i < len; i++) {
            if (wide) ((WCHAR *)data)[i] = (WCHAR)name[i];
            else ((char *)data)[i] = name[i];
        }
        return len;
    }
    case RIDI_DEVICEINFO: {
        UINT len = sizeof(RID_DEVICE_INFO);
        if (!data) { *size = len; return 0; }
        if (*size < len) { *size = len; SetLastError(ERROR_INSUFFICIENT_BUFFER); return (UINT)-1; }
        RID_DEVICE_INFO *ri = data;
        memset(ri, 0, len);
        ri->cbSize = len;
        if (mouse) {
            ri->dwType = RIM_TYPEMOUSE;
            ri->mouse.dwId = 2;                        /* MOUSE_I8042_HARDWARE */
            ri->mouse.dwNumberOfButtons = 5;
            ri->mouse.dwSampleRate = 100;
            ri->mouse.fHasHorizontalWheel = TRUE;
        } else {
            ri->dwType = RIM_TYPEKEYBOARD;
            ri->keyboard.dwType = 4;                   /* IBM enhanced (101- or 102-key) */
            ri->keyboard.dwKeyboardMode = 1;
            ri->keyboard.dwNumberOfFunctionKeys = 12;
            ri->keyboard.dwNumberOfIndicators = 3;
            ri->keyboard.dwNumberOfKeysTotal = 101;
        }
        return len;
    }
    case RIDI_PREPARSEDDATA:                           /* (not a HID device: none) */
        if (!data) { *size = 0; return 0; }
        SetLastError(ERROR_INVALID_HANDLE);
        return (UINT)-1;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return (UINT)-1;
}

/* A class device's: its interface path, attributes and collection, the
 * class driver's preparsed data */
static UINT class_device_info(const ClassDev *d, UINT cmd, LPVOID data, PUINT size, BOOL wide)
{
    switch (cmd) {
    case RIDI_DEVICENAME: {                            /* *size in characters */
        UINT len = (UINT)lstrlenW(d->path) + 1;
        if (!data) { *size = len; return 0; }
        if (*size < len) { *size = len; SetLastError(ERROR_INSUFFICIENT_BUFFER); return (UINT)-1; }
        for (UINT i = 0; i < len; i++) {
            if (wide) ((WCHAR *)data)[i] = d->path[i];
            else ((char *)data)[i] = (char)d->path[i];
        }
        return len;
    }
    case RIDI_DEVICEINFO: {
        UINT len = sizeof(RID_DEVICE_INFO);
        if (!data) { *size = len; return 0; }
        if (*size < len) { *size = len; SetLastError(ERROR_INSUFFICIENT_BUFFER); return (UINT)-1; }
        RID_DEVICE_INFO *ri = data;
        memset(ri, 0, len);
        ri->cbSize = len;
        ri->dwType = RIM_TYPEHID;
        ri->hid.dwVendorId = d->vid;
        ri->hid.dwProductId = d->pid;
        ri->hid.dwVersionNumber = d->version;
        ri->hid.usUsagePage = (USHORT)(d->usage >> 16);
        ri->hid.usUsage = (USHORT)d->usage;
        return len;
    }
    case RIDI_PREPARSEDDATA:
        if (!data) { *size = d->pp_len; return 0; }
        if (*size < d->pp_len) { *size = d->pp_len; SetLastError(ERROR_INSUFFICIENT_BUFFER); return (UINT)-1; }
        memcpy(data, d->pp, d->pp_len);
        return d->pp_len;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return (UINT)-1;
}

static UINT device_info(HANDLE dev, UINT cmd, LPVOID data, PUINT size, BOOL wide)
{
    if ((ULONG_PTR)dev > DEV_CLASS) {
        AcquireSRWLockShared(&g_class_lock);
        ClassDev *d = class_dev(dev);
        UINT r = d && size ? class_device_info(d, cmd, data, size, wide) : (UINT)-1;
        ReleaseSRWLockShared(&g_class_lock);
        if (!d) SetLastError(ERROR_INVALID_HANDLE);
        else if (!size) SetLastError(998 /* ERROR_NOACCESS */);
        return r;
    }
    if ((dev == DEV_MOUSE || dev == DEV_KEYBOARD) && !size) { SetLastError(998 /* ERROR_NOACCESS */); return (UINT)-1; }
    if (dev == DEV_MOUSE || dev == DEV_KEYBOARD) return input_device_info(dev == DEV_MOUSE, cmd, data, size, wide);
    NovaPadInfo info;
    int slot = dev_slot(dev, &info);
    if (slot < 0) { SetLastError(ERROR_INVALID_HANDLE); return (UINT)-1; }
    if (!size) { SetLastError(998 /* ERROR_NOACCESS */); return (UINT)-1; }
    switch (cmd) {
    case RIDI_DEVICENAME: {                            /* *size in characters */
        WCHAR w[96];
        UINT len = (UINT)nova_pad_path(&info, w, FALSE) + 1;
        if (!data) { *size = len; return 0; }
        if (*size < len) { *size = len; SetLastError(ERROR_INSUFFICIENT_BUFFER); return (UINT)-1; }
        for (UINT i = 0; i < len; i++) {
            if (wide) ((WCHAR *)data)[i] = w[i];
            else ((char *)data)[i] = (char)w[i];
        }
        return len;
    }
    case RIDI_DEVICEINFO: {
        UINT len = sizeof(RID_DEVICE_INFO);
        if (!data) { *size = len; return 0; }
        if (*size < len) { *size = len; SetLastError(ERROR_INSUFFICIENT_BUFFER); return (UINT)-1; }
        RID_DEVICE_INFO *ri = data;
        memset(ri, 0, len);
        ri->cbSize = len;
        ri->dwType = RIM_TYPEHID;
        ri->hid.dwVendorId = info.vid;
        ri->hid.dwProductId = info.pid;
        ri->hid.dwVersionNumber = info.kind == NOVA_PAD_HID ? 0x0100 : 0x0114;
        ri->hid.usUsagePage = (USHORT)(info.usage >> 8);
        ri->hid.usUsage = (USHORT)(info.usage & 0xFF);
        return len;
    }
    case RIDI_PREPARSEDDATA: {
        UINT len = nova_pad_preparsed(slot, NULL, 0);
        if (!len) { SetLastError(ERROR_INVALID_HANDLE); return (UINT)-1; }
        if (!data) { *size = len; return 0; }
        if (*size < len) { *size = len; SetLastError(ERROR_INSUFFICIENT_BUFFER); return (UINT)-1; }
        return nova_pad_preparsed(slot, data, *size);
    }
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return (UINT)-1;
}

USERAPI UINT GetRawInputDeviceInfoW(HANDLE dev, UINT cmd, LPVOID data, PUINT size) { return device_info(dev, cmd, data, size, TRUE); }
USERAPI UINT GetRawInputDeviceInfoA(HANDLE dev, UINT cmd, LPVOID data, PUINT size) { return device_info(dev, cmd, data, size, FALSE); }

/* The RAWINPUT of held input @h: its size (and into @out when given) */
static UINT raw_block(const Held *h, RAWINPUT *out)
{
    UINT size = (UINT)(sizeof(RAWINPUTHEADER) + (h->type == RIM_TYPEHID ? 2 * sizeof(DWORD) : 0) + h->len);
    if (out) {
        out->header.dwType = h->type;
        out->header.dwSize = size;
        out->header.hDevice = h->dev;
        out->header.wParam = h->code;
        if (h->type == RIM_TYPEMOUSE) { out->data.mouse = h->mouse; return size; }
        if (h->type == RIM_TYPEKEYBOARD) { out->data.keyboard = h->keyboard; return size; }
        out->data.hid.dwSizeHid = h->len;
        out->data.hid.dwCount = 1;
        memcpy(out->data.hid.bRawData, h->data, h->len);
    }
    return size;
}

USERAPI UINT GetRawInputData(HRAWINPUT raw, UINT cmd, LPVOID data, PUINT size, UINT header)
{
    if (header != sizeof(RAWINPUTHEADER) || !size || (cmd != RID_INPUT && cmd != RID_HEADER)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return (UINT)-1;
    }
    DWORD id = (DWORD)(ULONG_PTR)raw;
    AcquireSRWLockShared(&g_lock);
    Held h = g_held[id % HELD];
    ReleaseSRWLockShared(&g_lock);
    if (!id || h.id != id) { SetLastError(ERROR_INVALID_HANDLE); return (UINT)-1; }
    UINT need = cmd == RID_HEADER ? (UINT)sizeof(RAWINPUTHEADER) : raw_block(&h, NULL);
    if (!data) { *size = need; return 0; }
    if (*size < need) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return (UINT)-1; }
    if (cmd == RID_HEADER) {
        RAWINPUTHEADER *hd = data;
        hd->dwType = h.type;
        hd->dwSize = raw_block(&h, NULL);
        hd->hDevice = h.dev;
        hd->wParam = h.code;
    } else raw_block(&h, data);
    return need;
}

/* The calling thread's WM_INPUT messages, oldest first, as many as fit */
USERAPI UINT GetRawInputBuffer(PRAWINPUT data, PUINT size, UINT header)
{
    if (header != sizeof(RAWINPUTHEADER) || !size) { SetLastError(ERROR_INVALID_PARAMETER); return (UINT)-1; }
    MSG m;
    if (!data) {
        *size = 0;
        while (PeekMessageW(&m, NULL, WM_INPUT, WM_INPUT, PM_NOREMOVE)) {
            DWORD id = (DWORD)m.lParam;
            AcquireSRWLockShared(&g_lock);
            Held h = g_held[id % HELD];
            ReleaseSRWLockShared(&g_lock);
            if (h.id == id) { *size = raw_block(&h, NULL); break; }
            PeekMessageW(&m, NULL, WM_INPUT, WM_INPUT, PM_REMOVE);   /* (stale: dropped) */
        }
        return 0;
    }
    BYTE *at = (BYTE *)data, *end = at + *size;
    UINT n = 0;
    while (PeekMessageW(&m, NULL, WM_INPUT, WM_INPUT, PM_NOREMOVE)) {
        DWORD id = (DWORD)m.lParam;
        AcquireSRWLockShared(&g_lock);
        Held h = g_held[id % HELD];
        ReleaseSRWLockShared(&g_lock);
        if (h.id == id) {
            UINT need = raw_block(&h, NULL);
            if (at + need > end) {
                if (!n) { *size = need; SetLastError(ERROR_INSUFFICIENT_BUFFER); return (UINT)-1; }
                break;
            }
            raw_block(&h, (RAWINPUT *)at);
            at = (BYTE *)NEXTRAWINPUTBLOCK((RAWINPUT *)at);
            n++;
        }
        PeekMessageW(&m, NULL, WM_INPUT, WM_INPUT, PM_REMOVE);
        if (at >= end) break;
    }
    return n;
}

/* Raw input nobody handled: nothing more to do with it (header size checked as Windows does) */
USERAPI LRESULT DefRawInputProc(PRAWINPUT *raw, INT n, UINT header)
{
    (void)raw; (void)n;
    return header == sizeof(RAWINPUTHEADER) ? 0 : -1;
}

/* msg.c: whether the process takes the mouse's (or the keyboard's) input
 * only as raw input (RIDEV_NOLEGACY), not as mouse or key messages */
BOOL raw_nolegacy(BOOL keyboard)
{
    if (!g_nreg) return FALSE;
    AcquireSRWLockShared(&g_lock);
    RAWINPUTDEVICE *r = reg_for(keyboard ? USAGE_KEYBOARD : USAGE_MOUSE);
    BOOL no = r && MODE(r->dwFlags) == RIDEV_NOLEGACY;
    ReleaseSRWLockShared(&g_lock);
    return no;
}
