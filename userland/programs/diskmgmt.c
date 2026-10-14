/*
 * diskmgmt.exe — Disk Management: the volumes and the disks of this PC,
 * as Windows' Disk Management shows them, read and changed through the
 * public storage APIs
 *
 *   - the volumes: FindFirstVolume/FindNextVolume list them,
 *     GetVolumePathNamesForVolumeName gives each one's drive letter,
 *     GetVolumeInformation its label and file system, GetDiskFreeSpaceEx
 *     its capacity and free space, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS
 *     where it is on which disk;
 *   - the disks: \\.\PhysicalDriveN, IOCTL_STORAGE_QUERY_PROPERTY (the
 *     model and bus), IOCTL_DISK_GET_DRIVE_GEOMETRY_EX (the size) and
 *     IOCTL_DISK_GET_DRIVE_LAYOUT_EX (the partitions);
 *   - the changes: Initialize Disk (IOCTL_DISK_CREATE_DISK, GPT or MBR),
 *     New Simple Volume (IOCTL_DISK_SET_DRIVE_LAYOUT_EX with a partition
 *     added in the unallocated space, then formatted and given a letter),
 *     Format (fmifs.dll's FormatEx, NTFS or FAT32, after "Formatting this
 *     volume will erase all data on it") and Change Drive Letter
 *     (DeleteVolumeMountPoint, SetVolumeMountPoint).
 *
 * The volume list is on top; below it each disk is drawn as a row of its
 * partitions and its unallocated space.  NovaOS's own volumes (drive C:'s,
 * its boot files) are shown but not formatted or given other letters.
 *
 * "diskmgmt --test" (the devices suite's "diskmgr" boot, which attaches a
 * blank 256 MB disk) does with the blank disk what a person would with
 * the menus, through the same code: initializes it as GPT, makes a 128 MB
 * NTFS volume and a FAT32 volume from the rest, writes a file on each,
 * moves the FAT32 volume to drive T: and checks every step through the
 * APIs above.  "diskmgmt --list" logs the volumes and disks only.  Both
 * log with OutputDebugString ("diskmgmt: ..."), show the window with the
 * new disk selected ("diskmgmt: showing", the test's screenshot) and close
 * with "diskmgmt: N passed, M failed".
 */
#include <windows.h>
#include <commctrl.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define ID_LIST        100
#define ID_MAP         101
#define IDM_EXIT       200
#define IDM_REFRESH    201
#define IDM_LETTER     202
#define IDM_FORMAT     203
#define IDM_NEWVOL     204
#define IDM_INIT       205
#define TIMER_CHECK    1
#define TIMER_RESCAN   2
#define MAX_VOLS       64
#define MAX_DISKS      32
#define MAX_PARTS      16
#define MAX_FREE       8
#define MIB            (1024ull * 1024)
#define ROW_H          88
#define LABEL_W        150

typedef struct {
    WCHAR     name[64];                /* \\?\Volume{...}\ */
    WCHAR     letter;                  /* 0: none */
    WCHAR     label[34], fs[16];
    ULONGLONG total, free;
    DWORD     flags;                   /* NOVA_VOLUME_* */
    int       disk;                    /* index in g_disk, -1: none (drive C: in memory) */
    ULONGLONG off, len;
    BOOL      raw;                     /* no file system NovaOS knows */
} Vol;

typedef struct { ULONGLONG off, len; } Extent;

typedef struct {
    DWORD     number;                  /* \\.\PhysicalDriveN */
    WCHAR     model[64];
    ULONGLONG size;
    DWORD     style;                   /* PARTITION_STYLE_* (RAW: not initialized) */
    BOOL      removable;
    ULONGLONG first, end;              /* the space partitions may take */
    int       nparts;
    struct { ULONGLONG off, len; DWORD number; int vol; BOOL esp; BYTE mbr_type; } part[MAX_PARTS];
    int       nfree;
    Extent    free[MAX_FREE];
} Disk;

enum { SEL_NONE, SEL_DISK, SEL_VOL, SEL_FREE };
typedef struct { int kind, disk, vol, free; } Sel;

static HWND g_wnd, g_list, g_map;
static HFONT g_font, g_bold;
static Vol g_vol[MAX_VOLS];
static int g_nvol;
static Disk g_disk[MAX_DISKS];
static int g_ndisk;
static Sel g_sel;
static int g_scroll;
static BOOL g_test, g_listonly;
static int g_pass, g_fail;

static const GUID k_esp = { 0xC12A7328, 0xF81F, 0x11D2, { 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B } };
static const GUID k_basic = { 0xEBD0A0A2, 0xB9E5, 0x4433, { 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 } };

static void say(const char *fmt, ...)
{
    char buf[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

#define CHECK(what, cond) do { if (cond) g_pass++; else { g_fail++; say("FAIL: diskmgmt: %s (error %lu)\n", what, GetLastError()); } } while (0)

/* "256 MB", "1.50 GB", as Disk Management writes sizes */
static void size_text(ULONGLONG b, WCHAR *out, int cap)
{
    if (b < 1000 * MIB) _snwprintf(out, (size_t)cap, L"%llu MB", (b + MIB / 2) / MIB);
    else {
        ULONGLONG c = (b * 100 + 512 * MIB) / (1024 * MIB);       /* hundredths of a GB */
        if (c < 100000) _snwprintf(out, (size_t)cap, L"%llu.%02llu GB", c / 100, c % 100);
        else _snwprintf(out, (size_t)cap, L"%llu.%02llu TB", c / 102400, c % 102400 * 100 / 102400);
    }
}

/* ---- reading the volumes and disks ---- */

static HANDLE open_dev(const WCHAR *path, DWORD access)
{
    return CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

/* @name (\\?\Volume{...}\) opened as a device: without its backslash */
static HANDLE open_volume(const WCHAR *name, DWORD access)
{
    WCHAR dev[64];
    lstrcpynW(dev, name, 64);
    int n = lstrlenW(dev);
    if (n && dev[n - 1] == L'\\') dev[n - 1] = 0;
    return open_dev(dev, access);
}

static void read_volume(Vol *v)
{
    WCHAR paths[256];
    DWORD need = 0;
    v->letter = 0;
    if (GetVolumePathNamesForVolumeNameW(v->name, paths, 256, &need))
        for (WCHAR *p = paths; *p; p += lstrlenW(p) + 1)
            if (p[1] == L':' && !v->letter) v->letter = p[0];
    v->label[0] = v->fs[0] = 0;
    v->raw = !GetVolumeInformationW(v->name, v->label, 34, NULL, NULL, NULL, v->fs, 16);
    ULARGE_INTEGER avail, total, fr;
    v->total = v->free = 0;
    if (!v->raw && GetDiskFreeSpaceExW(v->name, &avail, &total, &fr)) { v->total = total.QuadPart; v->free = fr.QuadPart; }
    v->disk = -1;
    v->off = v->len = 0;
    v->flags = 0;
    HANDLE h = open_volume(v->name, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    BYTE buf[sizeof(VOLUME_DISK_EXTENTS) + 4 * sizeof(DISK_EXTENT)];
    VOLUME_DISK_EXTENTS *x = (VOLUME_DISK_EXTENTS *)buf;
    DWORD got;
    if (DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, buf, sizeof(buf), &got, NULL) && x->NumberOfDiskExtents) {
        for (int i = 0; i < g_ndisk; i++) if (g_disk[i].number == x->Extents[0].DiskNumber) v->disk = i;
        v->off = (ULONGLONG)x->Extents[0].StartingOffset.QuadPart;
        v->len = (ULONGLONG)x->Extents[0].ExtentLength.QuadPart;
    }
    NOVA_VOLUME_INFO ni;
    if (DeviceIoControl(h, IOCTL_NOVA_VOLUME_INFO, NULL, 0, &ni, sizeof(ni), &got, NULL)) {
        v->flags = ni.Flags;
        if (!v->len) v->len = ni.Length;
    }
    CloseHandle(h);
    if (v->raw) v->total = v->len;
}

static void free_space(Disk *d)
{
    d->nfree = 0;
    if (d->style == PARTITION_STYLE_RAW) return;
    ULONGLONG at = d->first;
    for (;;) {
        ULONGLONG next = d->end;               /* the next partition at or after @at */
        int k = -1;
        for (int i = 0; i < d->nparts; i++)
            if (d->part[i].off + d->part[i].len > at && d->part[i].off < next) { next = d->part[i].off; k = i; }
        ULONGLONG start = (at + MIB - 1) / MIB * MIB, stop = next / MIB * MIB;
        if (next < at) start = stop = 0;
        if (stop > start && stop - start >= MIB && d->nfree < MAX_FREE) {
            d->free[d->nfree].off = start;
            d->free[d->nfree].len = stop - start;
            d->nfree++;
        }
        if (k < 0) break;
        ULONGLONG past = d->part[k].off + d->part[k].len;
        at = past > at ? past : at + 1;
    }
    if (d->style == PARTITION_STYLE_MBR && d->nparts >= 4) d->nfree = 0;    /* (no room in the table) */
}

static BOOL read_disk(DWORD n, Disk *d)
{
    WCHAR path[32];
    _snwprintf(path, 32, L"\\\\.\\PhysicalDrive%lu", n);
    HANDLE h = open_dev(path, 0);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    memset(d, 0, sizeof(*d));
    d->number = n;
    DWORD got;
    STORAGE_PROPERTY_QUERY q;
    memset(&q, 0, sizeof(q));
    BYTE desc[512];
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), desc, sizeof(desc), &got, NULL)) {
        STORAGE_DEVICE_DESCRIPTOR *s = (STORAGE_DEVICE_DESCRIPTOR *)desc;
        d->removable = s->RemovableMedia;
        if (s->ProductIdOffset && s->ProductIdOffset < got) {
            const char *m = (const char *)desc + s->ProductIdOffset;
            int i = 0;
            for (; m[i] && i < 63 && s->ProductIdOffset + (DWORD)i < got; i++) d->model[i] = (BYTE)m[i];
            while (i && d->model[i - 1] == L' ') i--;
            d->model[i] = 0;
        }
    }
    DISK_GEOMETRY_EX g;
    if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &g, sizeof(g), &got, NULL)) d->size = (ULONGLONG)g.DiskSize.QuadPart;
    DWORD cap = 48 + 144 * 128;
    DRIVE_LAYOUT_INFORMATION_EX *l = malloc(cap);
    d->style = PARTITION_STYLE_RAW;
    if (l && DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, l, cap, &got, NULL)) {
        d->style = l->PartitionStyle;
        d->first = MIB;
        d->end = d->size;
        if (d->style == PARTITION_STYLE_GPT) {
            d->first = (ULONGLONG)l->Gpt.StartingUsableOffset.QuadPart;
            d->end = d->first + (ULONGLONG)l->Gpt.UsableLength.QuadPart;
        }
        for (DWORD i = 0; i < l->PartitionCount && d->nparts < MAX_PARTS; i++) {
            PARTITION_INFORMATION_EX *p = &l->PartitionEntry[i];
            if (!p->PartitionLength.QuadPart) continue;
            if (p->PartitionStyle == PARTITION_STYLE_MBR && p->Mbr.PartitionType == PARTITION_ENTRY_UNUSED) continue;
            d->part[d->nparts].off = (ULONGLONG)p->StartingOffset.QuadPart;
            d->part[d->nparts].len = (ULONGLONG)p->PartitionLength.QuadPart;
            d->part[d->nparts].number = p->PartitionNumber;
            d->part[d->nparts].esp = p->PartitionStyle == PARTITION_STYLE_GPT ? !memcmp(&p->Gpt.PartitionType, &k_esp, sizeof(GUID))
                                                                             : p->Mbr.PartitionType == 0xEF;
            d->part[d->nparts].mbr_type = p->PartitionStyle == PARTITION_STYLE_MBR ? p->Mbr.PartitionType : 0;
            d->part[d->nparts].vol = -1;
            d->nparts++;
        }
    }
    free(l);
    CloseHandle(h);
    free_space(d);
    return TRUE;
}

static void enumerate(void)
{
    g_ndisk = 0;
    for (DWORD n = 0; n < MAX_DISKS && g_ndisk < MAX_DISKS; n++)
        if (read_disk(n, &g_disk[g_ndisk])) g_ndisk++;
    g_nvol = 0;
    HANDLE f = FindFirstVolumeW(g_vol[0].name, 64);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            read_volume(&g_vol[g_nvol]);
            g_nvol++;
        } while (g_nvol < MAX_VOLS && FindNextVolumeW(f, g_vol[g_nvol].name, 64));
        FindVolumeClose(f);
    }
    for (int i = 0; i < g_nvol; i++) {
        Vol *v = &g_vol[i];
        if (v->disk < 0) continue;
        Disk *d = &g_disk[v->disk];
        for (int k = 0; k < d->nparts; k++) if (d->part[k].off == v->off) d->part[k].vol = i;
    }
}

/* ---- what the views show ---- */

static void vol_title(const Vol *v, WCHAR *out, int cap)
{
    if (v->letter) _snwprintf(out, (size_t)cap, L"%ls%ls(%lc:)", v->label, v->label[0] ? L" " : L"", v->letter);
    else if (v->label[0]) lstrcpynW(out, v->label, cap);
    else if (v->disk >= 0) {
        int part = 0;
        const Disk *d = &g_disk[v->disk];
        for (int k = 0; k < d->nparts; k++) if (d->part[k].off == v->off) part = (int)d->part[k].number;
        _snwprintf(out, (size_t)cap, L"(Disk %lu partition %d)", d->number, part);
    } else lstrcpynW(out, L"(Volume)", cap);
}

static BOOL vol_is_esp(const Vol *v)
{
    if (v->flags & NOVA_VOLUME_SYSTEM) return TRUE;
    if (v->disk < 0) return FALSE;
    const Disk *d = &g_disk[v->disk];
    for (int k = 0; k < d->nparts; k++) if (d->part[k].off == v->off) return d->part[k].esp;
    return FALSE;
}

static void vol_status(const Vol *v, WCHAR *out, int cap)
{
    const WCHAR *kind = v->disk >= 0 && g_disk[v->disk].style == PARTITION_STYLE_GPT ? L"Basic Data Partition" : L"Primary Partition";
    if (vol_is_esp(v)) kind = L"EFI System Partition";
    if (v->flags & NOVA_VOLUME_BOOT) _snwprintf(out, (size_t)cap, L"Healthy (Boot, %ls)", kind);
    else _snwprintf(out, (size_t)cap, L"Healthy (%ls)", kind);
}

static void vol_fs(const Vol *v, WCHAR *out, int cap)
{
    lstrcpynW(out, v->raw ? L"RAW" : v->fs, cap);
}

static void fill_list(void)
{
    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_list, LVM_DELETEALLITEMS, 0, 0);
    for (int i = 0; i < g_nvol; i++) {
        const Vol *v = &g_vol[i];
        WCHAR t[96];
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i;
        vol_title(v, t, 96);
        it.pszText = t;
        it.lParam = i;
        int row = (int)SendMessageW(g_list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        WCHAR cols[7][64];
        lstrcpyW(cols[0], L"Simple");
        lstrcpyW(cols[1], L"Basic");
        vol_fs(v, cols[2], 64);
        vol_status(v, cols[3], 64);
        size_text(v->total, cols[4], 64);
        if (v->raw) { lstrcpyW(cols[5], cols[4]); lstrcpyW(cols[6], L"100 %"); }
        else {
            size_text(v->free, cols[5], 64);
            _snwprintf(cols[6], 64, L"%llu %%", v->total ? v->free * 100 / v->total : 0);
        }
        for (int c = 0; c < 7; c++) {
            it.mask = LVIF_TEXT;
            it.iItem = row;
            it.iSubItem = c + 1;
            it.pszText = cols[c];
            SendMessageW(g_list, LVM_SETITEMTEXTW, row, (LPARAM)&it);
        }
    }
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, NULL, TRUE);
}

static void log_all(void)
{
    for (int i = 0; i < g_ndisk; i++) {
        const Disk *d = &g_disk[i];
        WCHAR sz[32];
        size_text(d->size, sz, 32);
        say("diskmgmt: Disk %lu: %ls, %ls, %ls%s, %d partition(s), %d unallocated\n", d->number, d->model[0] ? d->model : L"Disk",
            sz, d->style == PARTITION_STYLE_GPT ? L"GPT" : d->style == PARTITION_STYLE_MBR ? L"MBR" : L"Not Initialized",
            d->removable ? ", removable" : "", d->nparts, d->nfree);
    }
    for (int i = 0; i < g_nvol; i++) {
        const Vol *v = &g_vol[i];
        WCHAR t[96], st[64], fs[16], sz[32];
        vol_title(v, t, 96);
        vol_status(v, st, 64);
        vol_fs(v, fs, 16);
        size_text(v->total, sz, 32);
        say("diskmgmt: volume %ls: %ls, %ls, %ls, on %s%d at %llu MB\n", t, fs, sz, st, v->disk >= 0 ? "Disk " : "",
            v->disk >= 0 ? (int)g_disk[v->disk].number : -1, v->off / MIB);
    }
}

/* ---- the disk map ---- */

typedef struct { RECT r; int kind, vol, free; ULONGLONG len; } Box;

/* The boxes of disk @i's row (at @y): its partitions and its unallocated
 * space, in order on the disk, widths by size (each at least 90 pixels) */
static int boxes_of(int i, int y, int width, Box *out)
{
    const Disk *d = &g_disk[i];
    struct { ULONGLONG off, len; int kind, vol, free; } item[MAX_PARTS + MAX_FREE];
    int n = 0;
    for (int k = 0; k < d->nparts; k++) { item[n].off = d->part[k].off; item[n].len = d->part[k].len; item[n].kind = SEL_VOL; item[n].vol = d->part[k].vol; item[n].free = -1; n++; }
    for (int k = 0; k < d->nfree; k++) { item[n].off = d->free[k].off; item[n].len = d->free[k].len; item[n].kind = SEL_FREE; item[n].vol = -1; item[n].free = k; n++; }
    if (!n) {                                  /* not initialized: one unallocated box */
        out[0].r.left = LABEL_W + 4; out[0].r.top = y; out[0].r.right = width - 8; out[0].r.bottom = y + ROW_H - 8;
        out[0].kind = SEL_DISK; out[0].vol = out[0].free = -1; out[0].len = d->size;
        return 1;
    }
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (item[b].off < item[a].off) { __typeof__(item[0]) t = item[a]; item[a] = item[b]; item[b] = t; }
    int avail = width - 8 - (LABEL_W + 4) - 2 * (n - 1), minw = 90, rest = avail - minw * n;
    ULONGLONG total = 0;
    for (int k = 0; k < n; k++) total += item[k].len;
    int x = LABEL_W + 4;
    for (int k = 0; k < n; k++) {
        int w = minw + (rest > 0 && total ? (int)((ULONGLONG)rest * item[k].len / total) : 0);
        if (k == n - 1) w = width - 8 - x;
        out[k].r.left = x; out[k].r.top = y; out[k].r.right = x + w; out[k].r.bottom = y + ROW_H - 8;
        out[k].kind = item[k].kind; out[k].vol = item[k].vol; out[k].free = item[k].free; out[k].len = item[k].len;
        x += w + 2;
    }
    return n;
}

static void text_line(HDC dc, int x, int y, int w, const WCHAR *s, HFONT f)
{
    SelectObject(dc, f);
    RECT r = { x, y, x + w, y + 16 };
    DrawTextW(dc, s, -1, &r, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static void paint_map(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT cr;
    GetClientRect(h, &cr);
    HBRUSH bg = CreateSolidBrush(RGB(0xF3, 0xF3, 0xF3)), white = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF)),
           blue = CreateSolidBrush(RGB(0x1F, 0x4E, 0x9A)), black = CreateSolidBrush(RGB(0x10, 0x10, 0x10)),
           sel = CreateSolidBrush(RGB(0xCC, 0xE4, 0xF7)), grey = CreateSolidBrush(RGB(0xE1, 0xE1, 0xE1)),
           edge = CreateSolidBrush(RGB(0xA0, 0xA0, 0xA0)), dark = CreateSolidBrush(RGB(0x40, 0x40, 0x40));
    FillRect(dc, &cr, bg);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0x10, 0x10, 0x10));
    for (int i = 0; i < g_ndisk; i++) {
        const Disk *d = &g_disk[i];
        int y = 8 + i * ROW_H - g_scroll;
        if (y + ROW_H < 0 || y > cr.bottom) continue;
        RECT lab = { 8, y, LABEL_W, y + ROW_H - 8 };
        FillRect(dc, &lab, g_sel.kind == SEL_DISK && g_sel.disk == i ? sel : grey);
        FrameRect(dc, &lab, edge);
        WCHAR t[64], sz[32];
        _snwprintf(t, 64, L"Disk %lu", d->number);
        text_line(dc, 14, y + 6, LABEL_W - 18, t, g_bold);
        text_line(dc, 14, y + 24, LABEL_W - 18, d->style == PARTITION_STYLE_RAW ? L"Unknown" : d->removable ? L"Removable" : L"Basic", g_font);
        size_text(d->size, sz, 32);
        text_line(dc, 14, y + 42, LABEL_W - 18, sz, g_font);
        text_line(dc, 14, y + 60, LABEL_W - 18, d->style == PARTITION_STYLE_RAW ? L"Not Initialized" : L"Online", g_font);
        Box b[MAX_PARTS + MAX_FREE];
        int n = boxes_of(i, y, cr.right, b);
        for (int k = 0; k < n; k++) {
            RECT r = b[k].r, stripe = r, body = r;
            BOOL on = (b[k].kind == SEL_VOL && g_sel.kind == SEL_VOL && g_sel.vol == b[k].vol && b[k].vol >= 0) ||
                      (b[k].kind == SEL_FREE && g_sel.kind == SEL_FREE && g_sel.disk == i && g_sel.free == b[k].free) ||
                      (b[k].kind == SEL_DISK && g_sel.kind == SEL_DISK && g_sel.disk == i);
            stripe.bottom = r.top + 8;
            body.top = r.top + 8;
            FillRect(dc, &stripe, b[k].kind == SEL_VOL ? blue : black);
            FillRect(dc, &body, on ? sel : white);
            FrameRect(dc, &r, on ? dark : edge);
            int x = r.left + 6, w = r.right - r.left - 10;
            if (b[k].kind == SEL_VOL && b[k].vol >= 0) {
                const Vol *v = &g_vol[b[k].vol];
                WCHAR fs[16], st[64], line[64];
                vol_title(v, t, 64);
                vol_fs(v, fs, 16);
                vol_status(v, st, 64);
                size_text(v->len ? v->len : v->total, sz, 32);
                _snwprintf(line, 64, L"%ls %ls", sz, fs);
                text_line(dc, x, r.top + 14, w, t, g_bold);
                text_line(dc, x, r.top + 32, w, line, g_font);
                text_line(dc, x, r.top + 50, w, st, g_font);
            } else if (b[k].kind == SEL_VOL) {                    /* (a partition no volume answers for) */
                size_text(b[k].len, sz, 32);
                text_line(dc, x, r.top + 14, w, sz, g_font);
                text_line(dc, x, r.top + 32, w, L"Healthy (Partition)", g_font);
            } else {
                size_text(b[k].len, sz, 32);
                text_line(dc, x, r.top + 14, w, sz, g_font);
                text_line(dc, x, r.top + 32, w, L"Unallocated", g_font);
            }
        }
    }
    if (!g_ndisk) text_line(dc, 12, 12, cr.right - 24, L"No disks.", g_font);
    DeleteObject(bg); DeleteObject(white); DeleteObject(blue); DeleteObject(black);
    DeleteObject(sel); DeleteObject(grey); DeleteObject(edge); DeleteObject(dark);
    EndPaint(h, &ps);
}

static void select_list_row(int vol)
{
    LVITEMW it;
    memset(&it, 0, sizeof(it));
    it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(g_list, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&it);
    if (vol < 0) return;
    it.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(g_list, LVM_SETITEMSTATE, (WPARAM)vol, (LPARAM)&it);
    SendMessageW(g_list, LVM_ENSUREVISIBLE, (WPARAM)vol, FALSE);
}

static void set_sel(int kind, int disk, int vol, int fr)
{
    g_sel.kind = kind; g_sel.disk = disk; g_sel.vol = vol; g_sel.free = fr;
    InvalidateRect(g_map, NULL, FALSE);
}

/* What is at (@x, @y) in the map: selected */
static BOOL hit(int x, int y)
{
    RECT cr;
    GetClientRect(g_map, &cr);
    for (int i = 0; i < g_ndisk; i++) {
        int top = 8 + i * ROW_H - g_scroll;
        if (y < top || y >= top + ROW_H - 8) continue;
        if (x >= 8 && x < LABEL_W) { set_sel(SEL_DISK, i, -1, -1); select_list_row(-1); return TRUE; }
        Box b[MAX_PARTS + MAX_FREE];
        int n = boxes_of(i, top, cr.right, b);
        for (int k = 0; k < n; k++) {
            if (x < b[k].r.left || x >= b[k].r.right) continue;
            set_sel(b[k].kind, i, b[k].vol, b[k].free);
            select_list_row(b[k].kind == SEL_VOL ? b[k].vol : -1);
            return TRUE;
        }
    }
    return FALSE;
}

static void map_scrollbar(void)
{
    RECT cr;
    GetClientRect(g_map, &cr);
    SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMax = 8 + g_ndisk * ROW_H;
    si.nPage = (UINT)cr.bottom;
    if (g_scroll > si.nMax - (int)si.nPage) g_scroll = si.nMax - (int)si.nPage;
    if (g_scroll < 0) g_scroll = 0;
    si.nPos = g_scroll;
    SetScrollInfo(g_map, SB_VERT, &si, TRUE);
}

/* ---- the changes ---- */

static int vol_by_name(const WCHAR *name)
{
    for (int i = 0; i < g_nvol; i++) if (!lstrcmpiW(g_vol[i].name, name)) return i;
    return -1;
}

static int vol_at(int disk, ULONGLONG off)
{
    for (int i = 0; i < g_nvol; i++) if (g_vol[i].disk == disk && g_vol[i].off == off) return i;
    return -1;
}

static void refresh(void)
{
    WCHAR keep[64] = L"";
    DWORD keep_disk = (DWORD)-1;
    if (g_sel.kind == SEL_VOL && g_sel.vol >= 0 && g_sel.vol < g_nvol) lstrcpyW(keep, g_vol[g_sel.vol].name);
    if (g_sel.kind != SEL_NONE && g_sel.disk >= 0 && g_sel.disk < g_ndisk) keep_disk = g_disk[g_sel.disk].number;
    enumerate();
    fill_list();
    int disk = -1;
    for (int i = 0; i < g_ndisk; i++) if (g_disk[i].number == keep_disk) disk = i;
    int v = keep[0] ? vol_by_name(keep) : -1;
    if (v >= 0) { set_sel(SEL_VOL, g_vol[v].disk, v, -1); select_list_row(v); }
    else if (disk >= 0) set_sel(SEL_DISK, disk, -1, -1);
    else set_sel(SEL_NONE, -1, -1, -1);
    map_scrollbar();
    say("diskmgmt: showing %d volumes on %d disks\n", g_nvol, g_ndisk);
}

static BOOL refused(const Vol *v)
{
    return (v->flags & (NOVA_VOLUME_NOVA | NOVA_VOLUME_BOOT | NOVA_VOLUME_SYSTEM)) != 0;
}

/* Initialize disk @d (not initialized yet) as GPT or MBR */
static BOOL initialize_disk(int d, BOOL gpt)
{
    WCHAR path[32];
    _snwprintf(path, 32, L"\\\\.\\PhysicalDrive%lu", g_disk[d].number);
    HANDLE h = open_dev(path, GENERIC_READ | GENERIC_WRITE);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    CREATE_DISK c;
    memset(&c, 0, sizeof(c));
    c.PartitionStyle = gpt ? PARTITION_STYLE_GPT : PARTITION_STYLE_MBR;
    if (gpt) c.Gpt.MaxPartitionCount = 128;
    DWORD got;
    BOOL ok = DeviceIoControl(h, IOCTL_DISK_CREATE_DISK, &c, sizeof(c), NULL, 0, &got, NULL);
    if (ok) DeviceIoControl(h, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &got, NULL);
    DWORD err = GetLastError();
    CloseHandle(h);
    say("diskmgmt: Disk %lu initialized as %s: %s\n", g_disk[d].number, gpt ? "GPT" : "MBR", ok ? "done" : "failed");
    SetLastError(err);
    return ok;
}

/* The fmifs.dll callback: the outcome */
static BOOLEAN g_format_ok;
static BOOLEAN WINAPI format_callback(DWORD command, DWORD sub, PVOID info)
{
    (void)sub;
    if (command == 11 && info) g_format_ok = *(BOOLEAN *)info;      /* DONE */
    return TRUE;
}

/* Format volume @name (\\?\Volume{...}\) as NTFS or FAT32 labelled @label */
static BOOL format_volume(const WCHAR *name, BOOL ntfs, const WCHAR *label)
{
    typedef VOID (WINAPI *FormatExFn)(PWCHAR, DWORD, PWCHAR, PWCHAR, BOOL, DWORD, BOOLEAN (WINAPI *)(DWORD, DWORD, PVOID));
    HMODULE m = LoadLibraryW(L"fmifs.dll");
    FormatExFn fx = m ? (FormatExFn)(void *)GetProcAddress(m, "FormatEx") : NULL;
    if (!fx) return FALSE;
    WCHAR root[64], fs[8], lab[34];
    lstrcpynW(root, name, 64);
    lstrcpyW(fs, ntfs ? L"NTFS" : L"FAT32");
    lstrcpynW(lab, label, 34);
    g_format_ok = FALSE;
    fx(root, 12 /* FixedMedia */, fs, lab, TRUE, 0, format_callback);
    say("diskmgmt: format %ls as %ls \"%ls\": %s\n", name, fs, lab, g_format_ok ? "done" : "failed");
    return g_format_ok;
}

/* Give volume @name drive letter @letter (0: none), taking its old one away */
static BOOL change_letter(const WCHAR *name, WCHAR old, WCHAR letter)
{
    if (old == letter) return TRUE;
    if (old) {
        WCHAR m[4] = { old, L':', L'\\', 0 };
        if (!DeleteVolumeMountPointW(m)) return FALSE;
    }
    BOOL ok = TRUE;
    if (letter) {
        WCHAR m[4] = { letter, L':', L'\\', 0 };
        ok = SetVolumeMountPointW(m, name);
    }
    say("diskmgmt: drive letter of %ls: %lc: -> %lc: %s\n", name, old ? old : L'-', letter ? letter : L'-', ok ? "done" : "failed");
    return ok;
}

/* A new partition of @len bytes at @off on disk @d (in its unallocated
 * space), formatted as @ntfs/FAT32 labelled @label, with drive @letter (0:
 * the next free one; '-': none); its volume's name in @out */
static BOOL new_simple_volume(int d, ULONGLONG off, ULONGLONG len, WCHAR letter, BOOL ntfs, const WCHAR *label, WCHAR *out)
{
    WCHAR path[32];
    DWORD number = g_disk[d].number, got;
    _snwprintf(path, 32, L"\\\\.\\PhysicalDrive%lu", number);
    HANDLE h = open_dev(path, GENERIC_READ | GENERIC_WRITE);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD cap = 48 + 144 * 129;
    DRIVE_LAYOUT_INFORMATION_EX *l = calloc(1, cap);
    BOOL ok = l && DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, l, cap, &got, NULL);
    if (ok && l->PartitionStyle == PARTITION_STYLE_GPT) {
        PARTITION_INFORMATION_EX *p = &l->PartitionEntry[l->PartitionCount++];
        memset(p, 0, sizeof(*p));
        p->PartitionStyle = PARTITION_STYLE_GPT;
        p->StartingOffset.QuadPart = (LONGLONG)off;
        p->PartitionLength.QuadPart = (LONGLONG)len;
        p->RewritePartition = TRUE;
        p->Gpt.PartitionType = k_basic;                      /* (PartitionId zero: a new GUID) */
        lstrcpyW(p->Gpt.Name, L"Basic data partition");
    } else if (ok && l->PartitionStyle == PARTITION_STYLE_MBR) {
        PARTITION_INFORMATION_EX *p = NULL;
        for (DWORD i = 0; i < l->PartitionCount && !p; i++)
            if (l->PartitionEntry[i].Mbr.PartitionType == PARTITION_ENTRY_UNUSED) p = &l->PartitionEntry[i];
        if (!p && l->PartitionCount < 4) p = &l->PartitionEntry[l->PartitionCount++];
        ok = p != NULL;
        if (p) {
            DWORD num = (DWORD)(p - l->PartitionEntry) + 1;
            memset(p, 0, sizeof(*p));
            p->PartitionStyle = PARTITION_STYLE_MBR;
            p->StartingOffset.QuadPart = (LONGLONG)off;
            p->PartitionLength.QuadPart = (LONGLONG)len;
            p->PartitionNumber = num;
            p->RewritePartition = TRUE;
            p->Mbr.PartitionType = ntfs ? PARTITION_IFS : PARTITION_FAT32_XINT13;
            p->Mbr.RecognizedPartition = TRUE;
            p->Mbr.HiddenSectors = (DWORD)(off / 512);
        }
    } else ok = FALSE;
    if (ok) ok = DeviceIoControl(h, IOCTL_DISK_SET_DRIVE_LAYOUT_EX, l, 48 + 144 * l->PartitionCount, NULL, 0, &got, NULL);
    DWORD err = GetLastError();
    if (ok) DeviceIoControl(h, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &got, NULL);
    free(l);
    CloseHandle(h);
    say("diskmgmt: new partition on Disk %lu at %llu MB, %llu MB: %s\n", number, off / MIB, len / MIB, ok ? "done" : "failed");
    if (!ok) { SetLastError(err); return FALSE; }
    enumerate();
    int dn = -1;
    for (int i = 0; i < g_ndisk; i++) if (g_disk[i].number == number) dn = i;
    int v = dn >= 0 ? vol_at(dn, off) : -1;
    if (v < 0) { say("diskmgmt: the new partition has no volume\n"); return FALSE; }
    if (out) lstrcpyW(out, g_vol[v].name);
    WCHAR name[64];
    lstrcpyW(name, g_vol[v].name);
    if (!format_volume(name, ntfs, label)) return FALSE;
    v = vol_by_name(name);
    read_volume(&g_vol[v]);
    WCHAR now = g_vol[v].letter;
    if (letter == L'-') return change_letter(name, now, 0);
    if (letter && letter != now) return change_letter(name, now, letter);
    if (!now) {                                              /* (no letter of its own: the next free one) */
        DWORD used = GetLogicalDrives();
        for (int c = 3; c < 26; c++) if (!(used >> c & 1)) return change_letter(name, 0, (WCHAR)(L'A' + c));
    }
    return TRUE;
}

/* ---- the dialogs ---- */

typedef struct {
    const WCHAR *title;
    int result;                        /* IDOK / IDCANCEL */
    HWND ctl[8];
} Dlg;

static LRESULT CALLBACK dlg_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    Dlg *d = (Dlg *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    case WM_COMMAND:
        if (d && (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)) { d->result = LOWORD(wp); DestroyWindow(h); }
        return 0;
    case WM_CLOSE:
        if (d) d->result = IDCANCEL;
        DestroyWindow(h);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static HWND dlg_open(Dlg *d, int w, int hgt)
{
    RECT pr;
    GetWindowRect(g_wnd, &pr);
    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"NovaDiskDialog", d->title, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                             pr.left + (pr.right - pr.left - w) / 2, pr.top + (pr.bottom - pr.top - hgt) / 2, w, hgt, g_wnd, NULL,
                             GetModuleHandleW(NULL), d);
    d->result = 0;
    return h;
}

static HWND dlg_ctl(HWND dlg, const WCHAR *cls, const WCHAR *text, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(!lstrcmpW(cls, L"Edit") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, dlg,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, 0);
    return c;
}

static void dlg_buttons(HWND dlg, int w, int y, const WCHAR *ok)
{
    dlg_ctl(dlg, L"Button", ok, WS_TABSTOP | BS_DEFPUSHBUTTON, w - 196, y, 86, 26, IDOK);
    dlg_ctl(dlg, L"Button", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, w - 102, y, 86, 26, IDCANCEL);
}

/* Show @dlg modally; IDOK or IDCANCEL */
static int dlg_run(Dlg *d, HWND h)
{
    EnableWindow(g_wnd, FALSE);
    ShowWindow(h, SW_SHOW);
    MSG m;
    while (!d->result && GetMessageW(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN && (m.wParam == VK_RETURN || m.wParam == VK_ESCAPE) && (m.hwnd == h || GetParent(m.hwnd) == h)) {
            HWND focus = GetFocus();
            WCHAR cls[16] = L"";
            if (focus) GetClassNameW(focus, cls, 16);
            if (m.wParam == VK_ESCAPE || lstrcmpiW(cls, L"ComboBox")) {
                SendMessageW(h, WM_COMMAND, m.wParam == VK_RETURN ? IDOK : IDCANCEL, 0);
                continue;
            }
        }
        if (IsDialogMessageW(h, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    EnableWindow(g_wnd, TRUE);
    SetForegroundWindow(g_wnd);
    return d->result;
}

/* The drive letters a volume could take (and "(none)" first when @none);
 * @keep: its own, selected */
static void letter_choices(HWND combo, WCHAR keep, BOOL none)
{
    DWORD used = GetLogicalDrives();
    int sel = 0, n = 0;
    if (none) { SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"(none)"); n++; }
    for (int c = 3; c < 26; c++) {
        WCHAR l = (WCHAR)(L'A' + c);
        if ((used >> c & 1) && l != keep) continue;
        WCHAR t[3] = { l, 0 };
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)t);
        if (l == keep || (!keep && !none && n == 0)) sel = n;
        n++;
    }
    SendMessageW(combo, CB_SETCURSEL, (WPARAM)sel, 0);
}

static WCHAR combo_letter(HWND combo)
{
    WCHAR t[16] = L"";
    int i = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (i >= 0) SendMessageW(combo, CB_GETLBTEXT, (WPARAM)i, (LPARAM)t);
    return t[0] >= L'A' && t[0] <= L'Z' && !t[1] ? t[0] : 0;
}

static void fs_choices(HWND combo, BOOL fat_fits, BOOL ntfs_fits)
{
    if (ntfs_fits) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"NTFS");
    if (fat_fits) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"FAT32");
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

static BOOL combo_ntfs(HWND combo)
{
    WCHAR t[16] = L"";
    int i = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (i >= 0) SendMessageW(combo, CB_GETLBTEXT, (WPARAM)i, (LPARAM)t);
    return lstrcmpW(t, L"FAT32") != 0;
}

static void failed(const WCHAR *what)
{
    WCHAR t[300], e[160] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), 0, e, 160, NULL);
    _snwprintf(t, 300, L"%ls\n\n%ls", what, e[0] ? e : L"The operation failed.");
    MessageBoxW(g_wnd, t, L"Disk Management", MB_OK | MB_ICONERROR);
}

static void act_letter(void)
{
    if (g_sel.kind != SEL_VOL || g_sel.vol < 0) return;
    Vol *v = &g_vol[g_sel.vol];
    if (refused(v)) {
        MessageBoxW(g_wnd, L"NovaOS uses this volume: its drive letter cannot be changed.", L"Disk Management", MB_OK | MB_ICONINFORMATION);
        return;
    }
    WCHAR title[128], t[96];
    vol_title(v, t, 96);
    _snwprintf(title, 128, L"Change Drive Letter and Paths for %ls", t);
    Dlg d = { title };
    HWND h = dlg_open(&d, 420, 170);
    dlg_ctl(h, L"Static", L"Assign the following drive letter:", 0, 16, 18, 230, 20, 0);
    d.ctl[0] = dlg_ctl(h, L"ComboBox", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 250, 14, 140, 300, 10);
    letter_choices(d.ctl[0], v->letter, TRUE);
    dlg_ctl(h, L"Static", L"Programs that rely on drive letters might not run correctly.", 0, 16, 52, 380, 20, 0);
    dlg_buttons(h, 420, 92, L"OK");
    if (dlg_run(&d, h) != IDOK) return;
    WCHAR want = combo_letter(d.ctl[0]);
    if (!change_letter(v->name, v->letter, want)) failed(L"The drive letter could not be changed.");
    refresh();
}

static void act_format(void)
{
    if (g_sel.kind != SEL_VOL || g_sel.vol < 0) return;
    Vol *v = &g_vol[g_sel.vol];
    if (refused(v)) {
        MessageBoxW(g_wnd, L"NovaOS uses this volume: it cannot be formatted.", L"Disk Management", MB_OK | MB_ICONINFORMATION);
        return;
    }
    WCHAR title[128], t[96];
    vol_title(v, t, 96);
    _snwprintf(title, 128, L"Format %ls", t);
    Dlg d = { title };
    HWND h = dlg_open(&d, 400, 230);
    dlg_ctl(h, L"Static", L"Volume label:", 0, 16, 18, 130, 20, 0);
    d.ctl[0] = dlg_ctl(h, L"Edit", v->label[0] ? v->label : L"New Volume", WS_TABSTOP | ES_AUTOHSCROLL, 160, 14, 210, 24, 10);
    SendMessageW(d.ctl[0], EM_LIMITTEXT, 32, 0);
    dlg_ctl(h, L"Static", L"File system:", 0, 16, 54, 130, 20, 0);
    d.ctl[1] = dlg_ctl(h, L"ComboBox", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 160, 50, 210, 200, 11);
    fs_choices(d.ctl[1], v->len >= 34 * MIB, v->len >= 32 * MIB);
    dlg_ctl(h, L"Static", L"Allocation unit size:", 0, 16, 90, 140, 20, 0);
    HWND au = dlg_ctl(h, L"ComboBox", L"", CBS_DROPDOWNLIST, 160, 86, 210, 200, 12);
    SendMessageW(au, CB_ADDSTRING, 0, (LPARAM)L"Default");
    SendMessageW(au, CB_SETCURSEL, 0, 0);
    EnableWindow(au, FALSE);
    HWND quick = dlg_ctl(h, L"Button", L"Perform a quick format", BS_AUTOCHECKBOX, 16, 124, 250, 22, 13);
    SendMessageW(quick, BM_SETCHECK, BST_CHECKED, 0);
    EnableWindow(quick, FALSE);
    dlg_buttons(h, 400, 156, L"OK");
    SetFocus(d.ctl[0]);
    if (dlg_run(&d, h) != IDOK) return;
    WCHAR label[34];
    GetWindowTextW(d.ctl[0], label, 34);
    BOOL ntfs = combo_ntfs(d.ctl[1]);
    if (MessageBoxW(g_wnd, L"Formatting this volume will erase all data on it. Back up any data you want to keep before "
                           L"formatting. Do you want to continue?", L"Format", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
        return;
    if (!format_volume(v->name, ntfs, label)) failed(L"The format did not complete successfully.");
    refresh();
}

static void act_new_volume(void)
{
    if (g_sel.kind != SEL_FREE || g_sel.disk < 0) return;
    Disk *dk = &g_disk[g_sel.disk];
    Extent fr = dk->free[g_sel.free];
    ULONGLONG max = fr.len / MIB;
    WCHAR title[64], t[64];
    _snwprintf(title, 64, L"New Simple Volume on Disk %lu", dk->number);
    Dlg d = { title };
    HWND h = dlg_open(&d, 420, 270);
    _snwprintf(t, 64, L"Simple volume size in MB (1 - %llu):", max);
    dlg_ctl(h, L"Static", t, 0, 16, 18, 240, 20, 0);
    _snwprintf(t, 64, L"%llu", max);
    d.ctl[0] = dlg_ctl(h, L"Edit", t, WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 270, 14, 120, 24, 10);
    dlg_ctl(h, L"Static", L"Assign the following drive letter:", 0, 16, 54, 240, 20, 0);
    d.ctl[1] = dlg_ctl(h, L"ComboBox", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 270, 50, 120, 300, 11);
    letter_choices(d.ctl[1], 0, TRUE);
    SendMessageW(d.ctl[1], CB_SETCURSEL, 1, 0);
    dlg_ctl(h, L"Static", L"File system:", 0, 16, 90, 240, 20, 0);
    d.ctl[2] = dlg_ctl(h, L"ComboBox", L"", WS_TABSTOP | CBS_DROPDOWNLIST, 270, 86, 120, 200, 12);
    fs_choices(d.ctl[2], TRUE, TRUE);
    dlg_ctl(h, L"Static", L"Volume label:", 0, 16, 126, 240, 20, 0);
    d.ctl[3] = dlg_ctl(h, L"Edit", L"New Volume", WS_TABSTOP | ES_AUTOHSCROLL, 270, 122, 120, 24, 13);
    SendMessageW(d.ctl[3], EM_LIMITTEXT, 32, 0);
    dlg_ctl(h, L"Static", L"The volume is formatted with a quick format.", 0, 16, 162, 380, 20, 0);
    dlg_buttons(h, 420, 196, L"Finish");
    SetFocus(d.ctl[0]);
    if (dlg_run(&d, h) != IDOK) return;
    WCHAR num[24], label[34];
    GetWindowTextW(d.ctl[0], num, 24);
    GetWindowTextW(d.ctl[3], label, 34);
    ULONGLONG mb = wcstoull(num, NULL, 10);
    if (!mb || mb > max) { MessageBoxW(g_wnd, L"Enter a size the unallocated space holds.", L"Disk Management", MB_OK | MB_ICONERROR); return; }
    BOOL ntfs = combo_ntfs(d.ctl[2]);
    if (mb < (ntfs ? 32u : 34u)) {
        MessageBoxW(g_wnd, ntfs ? L"An NTFS volume needs at least 32 MB." : L"A FAT32 volume needs at least 34 MB.", L"Disk Management",
                    MB_OK | MB_ICONERROR);
        return;
    }
    WCHAR letter = combo_letter(d.ctl[1]);
    if (!new_simple_volume(g_sel.disk, fr.off, mb * MIB, letter ? letter : L'-', ntfs, label, NULL))
        failed(L"The volume could not be created.");
    refresh();
}

static void act_initialize(void)
{
    if (g_sel.disk < 0 || g_disk[g_sel.disk].style != PARTITION_STYLE_RAW) return;
    WCHAR t[96];
    _snwprintf(t, 96, L"You must initialize a disk before Disk Management can use it.\n\nDisk %lu", g_disk[g_sel.disk].number);
    Dlg d = { L"Initialize Disk" };
    HWND h = dlg_open(&d, 420, 230);
    dlg_ctl(h, L"Static", t, 0, 16, 14, 380, 52, 0);
    dlg_ctl(h, L"Static", L"Use the following partition style for the selected disk:", 0, 16, 76, 380, 20, 0);
    d.ctl[0] = dlg_ctl(h, L"Button", L"MBR (Master Boot Record)", WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON, 30, 100, 300, 22, 10);
    d.ctl[1] = dlg_ctl(h, L"Button", L"GPT (GUID Partition Table)", BS_AUTORADIOBUTTON, 30, 124, 300, 22, 11);
    SendMessageW(d.ctl[1], BM_SETCHECK, BST_CHECKED, 0);
    dlg_buttons(h, 420, 158, L"OK");
    if (dlg_run(&d, h) != IDOK) return;
    BOOL gpt = SendMessageW(d.ctl[1], BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!initialize_disk(g_sel.disk, gpt)) failed(L"The disk could not be initialized.");
    refresh();
}

static HMENU action_menu(HMENU m)
{
    while (GetMenuItemCount(m) > 0) DeleteMenu(m, 0, MF_BYPOSITION);
    BOOL vol = g_sel.kind == SEL_VOL && g_sel.vol >= 0, ok = vol && !refused(&g_vol[g_sel.vol]);
    BOOL fr = g_sel.kind == SEL_FREE;
    BOOL raw = g_sel.kind != SEL_NONE && g_sel.disk >= 0 && g_sel.disk < g_ndisk && g_disk[g_sel.disk].style == PARTITION_STYLE_RAW;
    AppendMenuW(m, MF_STRING | (raw ? 0 : MF_GRAYED), IDM_INIT, L"&Initialize Disk");
    AppendMenuW(m, MF_STRING | (fr ? 0 : MF_GRAYED), IDM_NEWVOL, L"New &Simple Volume...");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (ok ? 0 : MF_GRAYED), IDM_LETTER, L"Change Drive &Letter and Paths...");
    AppendMenuW(m, MF_STRING | (ok ? 0 : MF_GRAYED), IDM_FORMAT, L"&Format...");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_REFRESH, L"&Refresh\tF5");
    return m;
}

static void context_menu(HWND owner, int x, int y)
{
    HMENU m = action_menu(CreatePopupMenu());
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_RIGHTBUTTON, x, y, 0, owner == g_map ? g_wnd : owner, NULL);
    DestroyMenu(m);
}

/* ---- --test: the blank disk, through the same code as the menus ---- */

static BOOL write_file(const WCHAR *path, const char *text)
{
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    DWORD n;
    BOOL ok = WriteFile(f, text, (DWORD)strlen(text), &n, NULL) && n == strlen(text);
    CloseHandle(f);
    return ok;
}

static BOOL read_file(const WCHAR *path, const char *want)
{
    char buf[256];
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    DWORD n = 0;
    BOOL ok = ReadFile(f, buf, sizeof(buf) - 1, &n, NULL);
    CloseHandle(f);
    buf[n] = 0;
    return ok && !strcmp(buf, want);
}

/* Volume @name as the APIs report it: file system @fs, label @label, a
 * letter (returned), files written and read back */
static WCHAR check_volume(const WCHAR *name, const WCHAR *fs, const WCHAR *label, ULONGLONG len, const char *text)
{
    int v = vol_by_name(name);
    CHECK("FindFirstVolume/FindNextVolume list the new volume", v >= 0);
    if (v < 0) return 0;
    Vol *x = &g_vol[v];
    say("diskmgmt: new volume %ls: %c: %ls \"%ls\", %llu MB, %llu MB free\n", name, x->letter ? (char)x->letter : '-', x->fs, x->label,
        x->total / MIB, x->free / MIB);
    CHECK("GetVolumeInformation gives the file system it was formatted with", !lstrcmpW(x->fs, fs));
    CHECK("GetVolumeInformation gives its label", !lstrcmpW(x->label, label));
    CHECK("GetDiskFreeSpaceEx gives a capacity near the partition's size", x->total <= len && x->total > len * 3 / 4);
    CHECK("GetDiskFreeSpaceEx gives most of it free", x->free > x->total / 2 && x->free <= x->total);
    CHECK("the volume has a drive letter (GetVolumePathNamesForVolumeName)", x->letter != 0);
    if (!x->letter) return 0;
    WCHAR root[4] = { x->letter, L':', L'\\', 0 }, back[64] = L"", dev[64] = L"", dos[3] = { x->letter, L':', 0 }, path[64];
    CHECK("GetVolumeNameForVolumeMountPoint gives the letter's volume", GetVolumeNameForVolumeMountPointW(root, back, 64) &&
          !lstrcmpiW(back, name));
    CHECK("QueryDosDevice gives \\Device\\HarddiskVolumeN", QueryDosDeviceW(dos, dev, 64) && !wcsncmp(dev, L"\\Device\\HarddiskVolume", 22));
    CHECK("GetDriveType says fixed", GetDriveTypeW(root) == DRIVE_FIXED);
    _snwprintf(path, 64, L"%lc:\\Disk Management.txt", x->letter);
    CHECK("a file is written on the new volume", write_file(path, text));
    CHECK("and read back", read_file(path, text));
    _snwprintf(path, 64, L"%lc:\\New Folder", x->letter);
    CHECK("a folder is made on the new volume", CreateDirectoryW(path, NULL));
    _snwprintf(path, 64, L"%lc:\\New Folder\\inside.txt", x->letter);
    CHECK("a file is written in the folder", write_file(path, "inside\r\n"));
    return x->letter;
}

static int blank_disk(void)
{
    for (int i = 0; i < g_ndisk; i++)
        if (g_disk[i].style == PARTITION_STYLE_RAW && g_disk[i].size == 256 * MIB) return i;
    return -1;
}

static void run_test(void)
{
    int d = blank_disk();
    CHECK("a blank 256 MB disk shows, not initialized", d >= 0);
    if (d < 0) return;
    DWORD number = g_disk[d].number;
    CHECK("its model comes from IOCTL_STORAGE_QUERY_PROPERTY", g_disk[d].model[0] != 0);
    CHECK("NovaOS's own volumes may not be formatted", ({
        BOOL r = TRUE;
        for (int i = 0; i < g_nvol; i++) if (refused(&g_vol[i]) && g_vol[i].disk >= 0) { r = !format_volume(g_vol[i].name, TRUE, L"X"); break; }
        r; }));
    set_sel(SEL_DISK, d, -1, -1);
    CHECK("Initialize Disk (GPT)", initialize_disk(d, TRUE));
    refresh();
    d = -1;
    for (int i = 0; i < g_ndisk; i++) if (g_disk[i].number == number) d = i;
    CHECK("the disk is GPT now (IOCTL_DISK_GET_DRIVE_LAYOUT_EX)", d >= 0 && g_disk[d].style == PARTITION_STYLE_GPT);
    if (d < 0) return;
    CHECK("all of it is unallocated, from 1 MB", g_disk[d].nparts == 0 && g_disk[d].nfree == 1 && g_disk[d].free[0].off == MIB &&
          g_disk[d].free[0].len >= 254 * MIB);
    if (g_disk[d].nfree != 1) return;
    WCHAR ntfs[64] = L"", fat[64] = L"";
    ULONGLONG off = g_disk[d].free[0].off;
    CHECK("New Simple Volume: 128 MB, NTFS", new_simple_volume(d, off, 128 * MIB, 0, TRUE, L"NOVANTFS", ntfs));
    refresh();
    WCHAR l1 = check_volume(ntfs, L"NTFS", L"NOVANTFS", 128 * MIB, "Written on NTFS by Disk Management\r\n");
    CHECK("one unallocated space is left after it", g_disk[d].nfree == 1 && g_disk[d].free[0].off == off + 128 * MIB);
    if (g_disk[d].nfree != 1) return;
    Extent rest = g_disk[d].free[0];
    CHECK("New Simple Volume: the rest, FAT32", new_simple_volume(d, rest.off, rest.len, 0, FALSE, L"NOVAFAT", fat));
    refresh();
    WCHAR l2 = check_volume(fat, L"FAT32", L"NOVAFAT", rest.len, "Written on FAT32 by Disk Management\r\n");
    CHECK("the volumes have different letters", l1 && l2 && l1 != l2);
    CHECK("the disk has two partitions and no unallocated space", g_disk[d].nparts == 2 && g_disk[d].nfree == 0);
    int v = vol_by_name(fat);
    if (v < 0 || !l2) return;
    set_sel(SEL_VOL, d, v, -1);
    CHECK("Change Drive Letter to T:", change_letter(fat, l2, L'T'));
    refresh();
    v = vol_by_name(fat);
    CHECK("the FAT32 volume is T: now", v >= 0 && g_vol[v].letter == L'T');
    WCHAR old[64];
    _snwprintf(old, 64, L"%lc:\\Disk Management.txt", l2);
    CHECK("its files are on T:", read_file(L"T:\\Disk Management.txt", "Written on FAT32 by Disk Management\r\n"));
    CHECK("and no longer on the old letter", GetFileAttributesW(old) == INVALID_FILE_ATTRIBUTES);
    CHECK("T: is in GetLogicalDrives", GetLogicalDrives() >> 19 & 1);
    CHECK("SetVolumeMountPoint refuses a letter in use", ({
        WCHAR m[4] = { l1, L':', L'\\', 0 };
        !SetVolumeMountPointW(m, fat); }));
    if (v >= 0) { set_sel(SEL_VOL, d, v, -1); select_list_row(v); }
}

/* ---- the window ---- */

static void layout(void)
{
    RECT r;
    GetClientRect(g_wnd, &r);
    int top = r.bottom * 2 / 5;
    MoveWindow(g_list, 0, 0, r.right, top, TRUE);
    MoveWindow(g_map, 0, top + 4, r.right, r.bottom - top - 4, TRUE);
    map_scrollbar();
}

static LRESULT CALLBACK map_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT:
        paint_map(h);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(h);
        hit((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_RBUTTONUP: {
        if (!hit((short)LOWORD(lp), (short)HIWORD(lp))) return 0;
        POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ClientToScreen(h, &p);
        context_menu(h, p.x, p.y);
        return 0;
    }
    case WM_VSCROLL: {
        RECT cr;
        GetClientRect(h, &cr);
        int was = g_scroll;
        switch (LOWORD(wp)) {
        case SB_LINEUP: g_scroll -= 30; break;
        case SB_LINEDOWN: g_scroll += 30; break;
        case SB_PAGEUP: g_scroll -= cr.bottom; break;
        case SB_PAGEDOWN: g_scroll += cr.bottom; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: g_scroll = (short)HIWORD(wp); break;
        }
        map_scrollbar();
        if (was != g_scroll) InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL:
        g_scroll -= (short)HIWORD(wp) / 4;
        map_scrollbar();
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (g_list) layout();
        return 0;
    case WM_NOTIFY: {
        NMHDR *n = (NMHDR *)lp;
        if (n->idFrom == ID_LIST && n->code == LVN_ITEMCHANGED) {
            NMLISTVIEW *lv = (NMLISTVIEW *)lp;
            if ((lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED) && lv->iItem >= 0 && lv->iItem < g_nvol)
                set_sel(SEL_VOL, g_vol[lv->iItem].disk, lv->iItem, -1);
        } else if (n->idFrom == ID_LIST && n->code == NM_RCLICK && g_sel.kind == SEL_VOL) {
            POINT p;
            GetCursorPos(&p);
            context_menu(h, p.x, p.y);
        }
        return 0;
    }
    case WM_INITMENUPOPUP:
        if (HIWORD(lp) == 0 && LOWORD(lp) == 1) action_menu((HMENU)wp);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_EXIT: DestroyWindow(h); break;
        case IDM_REFRESH: refresh(); break;
        case IDM_LETTER: act_letter(); break;
        case IDM_FORMAT: act_format(); break;
        case IDM_NEWVOL: act_new_volume(); break;
        case IDM_INIT: act_initialize(); break;
        }
        return 0;
    case WM_DEVICECHANGE:                       /* (a disk plugged in or out: list them again once it settles) */
        SetTimer(h, TIMER_RESCAN, 500, NULL);
        return TRUE;
    case WM_TIMER:
        KillTimer(h, wp);
        if (wp == TIMER_RESCAN) refresh();
        else if (wp == TIMER_CHECK) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static HMENU make_menu(void)
{
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), action = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"E&xit");
    action_menu(action);
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)action, L"&Action");
    return bar;
}

static void add_column(int i, const WCHAR *name, int w, BOOL right)
{
    LVCOLUMNW col;
    memset(&col, 0, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = right ? LVCFMT_RIGHT : LVCFMT_LEFT;
    col.cx = w;
    col.pszText = (WCHAR *)name;
    SendMessageW(g_list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
}

int main(int argc, char **argv)
{
    HINSTANCE inst = GetModuleHandleW(NULL);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--test")) g_test = TRUE;
        if (!strcmp(argv[i], "--list")) g_listonly = TRUE;
    }
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"NovaDiskManagement";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = map_proc;
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"NovaDiskMap";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = dlg_proc;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"NovaDiskDialog";
    RegisterClassExW(&wc);
    g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    GetObjectW(g_font, sizeof(lf), &lf);
    lf.lfWeight = FW_BOLD;
    g_bold = CreateFontIndirectW(&lf);
    g_wnd = CreateWindowExW(0, L"NovaDiskManagement", L"Disk Management", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                            CW_USEDEFAULT, 1000, 660, NULL, make_menu(), inst, NULL);
    if (!g_wnd) { say("diskmgmt: no window\n"); return 1; }
    g_list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_NOSORTHEADER | LVS_SINGLESEL |
                             LVS_SHOWSELALWAYS, 0, 0, 10, 10, g_wnd, (HMENU)ID_LIST, inst, NULL);
    g_map = CreateWindowExW(0, L"NovaDiskMap", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 10, 10, g_wnd, (HMENU)ID_MAP, inst, NULL);
    SendMessageW(g_list, WM_SETFONT, (WPARAM)g_font, 0);
    SendMessageW(g_list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
    add_column(0, L"Volume", 200, FALSE);
    add_column(1, L"Layout", 70, FALSE);
    add_column(2, L"Type", 60, FALSE);
    add_column(3, L"File System", 90, FALSE);
    add_column(4, L"Status", 260, FALSE);
    add_column(5, L"Capacity", 90, TRUE);
    add_column(6, L"Free Space", 90, TRUE);
    add_column(7, L"% Free", 70, TRUE);
    layout();
    refresh();
    if (g_test || g_listonly) {
        log_all();
        CHECK("FindFirstVolume lists the volumes", g_nvol > 0);
        CHECK("\\\\.\\PhysicalDrive0 opens", g_ndisk > 0);
        if (g_test) {
            run_test();
            log_all();
        }
        say("diskmgmt: %d passed, %d failed\n", g_pass, g_fail);
        SetTimer(g_wnd, TIMER_CHECK, 8000, NULL);
    }
    ShowWindow(g_wnd, SW_SHOWNORMAL);
    UpdateWindow(g_wnd);
    if (g_test || g_listonly) say("diskmgmt: showing the disks\n");
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_F5) { refresh(); continue; }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return (g_test || g_listonly) && g_fail ? 1 : 0;
}
