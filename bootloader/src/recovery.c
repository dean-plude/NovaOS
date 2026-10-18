/*
 * recovery.c — NovaOS's Advanced startup menu (roadmap step 33.3)
 *
 * The boot loader's counterpart of Windows' Advanced Boot Options (F8)
 * and of the Windows Recovery Environment that comes up by itself after
 * two failed starts (docs/design/recovery.md).  On an installed disk it
 * shows, before NovaOS starts:
 *
 *   - when F8 is pressed (or held) as the loader starts;
 *   - when NovaOS asked for it before restarting (shutdown /r /o, which
 *     leaves the mark \EFI\NOVA\recovery.req, as Windows' "Restart to
 *     Advanced startup" sets a one-time boot of its recovery);
 *   - by itself when the starts before this one did not reach the desktop
 *     BOOT_FAILED_STARTS times in a row: every start adds one to the count
 *     in \EFI\NOVA\starts.cnt, and the kernel deletes the file once its
 *     desktop is up (kernel/fs/recovery.c), as Windows' boot manager
 *     counts boots that never reached logon in bootstat.dat.  Then the
 *     menu says so and runs startup repair unless something else is
 *     chosen within RECOVERY_SECONDS.
 *
 * Its lines: start NovaOS normally; start the previous version (the
 * known-good set an update left: \EFI\NOVA\kernel.old, and the boot
 * loader it came with, \EFI\NOVA\bootx64.old, which this loader starts
 * with the load option "previous" so it starts kernel.old without a menu);
 * safe mode; startup repair.  Text only, through the firmware's console,
 * which QEMU's firmware copies to the serial port (the boot test
 * rollbacktest reads it there).
 */

#include "../include/efi.h"
#include "../../include/boot_protocol.h"
#include "recovery.h"

void console_printf(const char *fmt, ...);

#define RECOVERY_SECONDS 30          /* the countdown to startup repair after failed starts */
#define KEY_WINDOW_MS    300         /* how long F8 is looked for */

typedef struct { UINT16 ScanCode; CHAR16 UnicodeChar; } RecKey;
typedef struct _RecInput RecInput;
struct _RecInput {
    EFI_STATUS (__attribute__((ms_abi)) *Reset)(RecInput *self, BOOLEAN ext);
    EFI_STATUS (__attribute__((ms_abi)) *ReadKeyStroke)(RecInput *self, RecKey *key);
    VOID *WaitForKey;
};
#define SCAN_UP   0x01
#define SCAN_DOWN 0x02
#define SCAN_F8   0x12

typedef EFI_STATUS (__attribute__((ms_abi)) *LOAD_IMAGE)(BOOLEAN boot_policy, EFI_HANDLE parent, VOID *path,
                                                         VOID *source, UINTN size, EFI_HANDLE *image);
typedef EFI_STATUS (__attribute__((ms_abi)) *START_IMAGE)(EFI_HANDLE image, UINTN *exit_size, CHAR16 **exit_data);
typedef EFI_STATUS (__attribute__((ms_abi)) *STALL)(UINTN us);

#define DEVICE_PATH_GUID \
    { 0x09576e91, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

static CHAR16 STARTS_PATH[] = { '\\','E','F','I','\\','N','O','V','A','\\','s','t','a','r','t','s','.','c','n','t', 0 };
static CHAR16 REQUEST_PATH[] = { '\\','E','F','I','\\','N','O','V','A','\\','r','e','c','o','v','e','r','y','.','r','e','q', 0 };
static CHAR16 OLD_KERNEL_PATH[] = { '\\','E','F','I','\\','N','O','V','A','\\','k','e','r','n','e','l','.','o','l','d', 0 };
static CHAR16 OLD_LOADER_PATH[] = { '\\','E','F','I','\\','N','O','V','A','\\','b','o','o','t','x','6','4','.','o','l','d', 0 };
static CHAR16 DRIVE_C_PATH[] = { '\\','N','O','V','A','\\','C', 0 };
static CHAR16 PREVIOUS_OPTION[] = { 'p','r','e','v','i','o','u','s', 0 };

static EFI_BOOT_SERVICES *g_rbs;
static EFI_HANDLE g_rimage, g_rdev;

static UINTN wlen(const CHAR16 *s) { UINTN n = 0; while (s[n]) n++; return n; }

static EFI_STATUS open_file(CHAR16 *path, UINT64 mode, EFI_FILE_PROTOCOL **f)
{
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE_PROTOCOL *root;
    EFI_STATUS st = g_rbs->OpenProtocol(g_rdev, &fs_guid, (VOID **)&fs, g_rimage, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(st)) return st;
    st = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(st)) return st;
    st = root->Open(root, f, path, mode, 0);
    root->Close(root);
    return st;
}

static BOOLEAN exists(CHAR16 *path)
{
    EFI_FILE_PROTOCOL *f;
    if (EFI_ERROR(open_file(path, EFI_FILE_MODE_READ, &f))) return FALSE;
    f->Close(f);
    return TRUE;
}

/* The failed-start count: one digit, 0 when there is no file */
static int read_count(void)
{
    EFI_FILE_PROTOCOL *f;
    char c = 0;
    UINTN n = 1;
    if (EFI_ERROR(open_file(STARTS_PATH, EFI_FILE_MODE_READ, &f))) return 0;
    EFI_STATUS st = f->Read(f, &n, &c);
    f->Close(f);
    return !EFI_ERROR(st) && n == 1 && c >= '0' && c <= '9' ? c - '0' : 0;
}

static void write_count(int count)
{
    EFI_FILE_PROTOCOL *f;
    char c = (char)('0' + (count > 9 ? 9 : count));
    UINTN n = 1;
    if (EFI_ERROR(open_file(STARTS_PATH, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, &f))) return;
    f->Write(f, &n, &c);
    f->Close(f);
}

/* The mark NovaOS left for this start (shutdown /r /o): taken away, so
 * the start after this one is an ordinary one again */
static BOOLEAN take_request(void)
{
    EFI_FILE_PROTOCOL *f;
    if (EFI_ERROR(open_file(REQUEST_PATH, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, &f))) return FALSE;
    f->Delete(f);
    return TRUE;
}

/* Whether this loader was started with the load option "previous" (by a
 * newer loader's menu, starting the known-good set) */
static BOOLEAN started_as_previous(void)
{
    EFI_GUID li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *li;
    if (EFI_ERROR(g_rbs->OpenProtocol(g_rimage, &li_guid, (VOID **)&li, g_rimage, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL)) ||
        !li->LoadOptions || li->LoadOptionsSize < sizeof(PREVIOUS_OPTION) - sizeof(CHAR16))
        return FALSE;
    const CHAR16 *o = (const CHAR16 *)li->LoadOptions;
    for (UINTN i = 0; PREVIOUS_OPTION[i]; i++)
        if (o[i] != PREVIOUS_OPTION[i]) return FALSE;
    return TRUE;
}

/* Whether \EFI\NOVA\bootx64.old knows the load option "previous" (has
 * the string): a loader from before this menu would start kernel.elf */
static BOOLEAN old_loader_knows_previous(void)
{
    EFI_FILE_PROTOCOL *f;
    if (EFI_ERROR(open_file(OLD_LOADER_PATH, EFI_FILE_MODE_READ, &f))) return FALSE;
    UINT8 *buf = NULL;
    UINTN size = 4u << 20;
    BOOLEAN found = FALSE;
    if (!EFI_ERROR(g_rbs->AllocatePool(EfiLoaderData, size, (VOID **)&buf)) && !EFI_ERROR(f->Read(f, &size, buf))) {
        const UINT8 *want = (const UINT8 *)PREVIOUS_OPTION;
        UINTN wl = sizeof(PREVIOUS_OPTION);
        for (UINTN i = 0; !found && i + wl <= size; i++) {
            UINTN k = 0;
            while (k < wl && buf[i + k] == want[k]) k++;
            found = k == wl;
        }
    }
    if (buf) g_rbs->FreePool(buf);
    f->Close(f);
    return found;
}

/* Start the previous version's boot loader, \EFI\NOVA\bootx64.old, with
 * the load option "previous"; returns only if it could not be started */
static void start_previous_loader(void)
{
    if (!old_loader_knows_previous()) {
        console_printf("The previous version's boot loader has no Advanced startup: this one starts kernel.old\r\n");
        return;
    }
    EFI_GUID dp_guid = DEVICE_PATH_GUID, li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    UINT8 *dp = NULL;
    if (EFI_ERROR(g_rbs->OpenProtocol(g_rdev, &dp_guid, (VOID **)&dp, g_rimage, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL)) || !dp)
        return;
    UINTN n = 0;
    for (int k = 0; k < 64; k++) {                         /* up to the end node */
        UINT16 len = (UINT16)(dp[n + 2] | dp[n + 3] << 8);
        if (dp[n] == 0x7F || len < 4) break;
        n += len;
    }
    UINTN flen = 4 + 2 * (wlen(OLD_LOADER_PATH) + 1);
    UINT8 *path = NULL;
    if (EFI_ERROR(g_rbs->AllocatePool(EfiLoaderData, n + flen + 4, (VOID **)&path))) return;
    for (UINTN i = 0; i < n; i++) path[i] = dp[i];
    UINT8 *fp = path + n;
    fp[0] = 4; fp[1] = 4; fp[2] = (UINT8)flen; fp[3] = (UINT8)(flen >> 8);   /* a file path node */
    for (UINTN i = 0; i <= wlen(OLD_LOADER_PATH); i++) {
        fp[4 + 2 * i] = (UINT8)OLD_LOADER_PATH[i];
        fp[5 + 2 * i] = (UINT8)(OLD_LOADER_PATH[i] >> 8);
    }
    UINT8 *end = fp + flen;
    end[0] = 0x7F; end[1] = 0xFF; end[2] = 4; end[3] = 0;

    EFI_HANDLE img = NULL;
    EFI_STATUS st = ((LOAD_IMAGE)g_rbs->LoadImage)(FALSE, g_rimage, path, NULL, 0, &img);
    g_rbs->FreePool(path);
    if (EFI_ERROR(st)) {
        console_printf("The previous version's boot loader could not be loaded (status %x)\r\n", (UINT64)st);
        return;
    }
    EFI_LOADED_IMAGE_PROTOCOL *li;
    if (!EFI_ERROR(g_rbs->OpenProtocol(img, &li_guid, (VOID **)&li, g_rimage, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL))) {
        li->LoadOptions = PREVIOUS_OPTION;
        li->LoadOptionsSize = (UINT32)sizeof(PREVIOUS_OPTION);
    }
    console_printf("Starting the previous version's boot loader (\\EFI\\NOVA\\bootx64.old)\r\n");
    st = ((START_IMAGE)g_rbs->StartImage)(img, NULL, NULL);
    console_printf("The previous version's boot loader returned (status %x)\r\n", (UINT64)st);
}

typedef struct { const char *name; int choice; } Line;

static void draw(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *out, const Line *m, int n, int sel, int left, int failed)
{
    out->ClearScreen(out);
    console_printf("NovaOS Advanced startup\r\n\r\n");
    if (failed)
        console_printf("NovaOS didn't start correctly: the last %u starts stopped before the desktop.\r\n\r\n",
                       (UINT64)failed);
    console_printf("Choose how to start:\r\n\r\n");
    for (int i = 0; i < n; i++)
        console_printf("  %s %u. %s\r\n", i == sel ? (const char *)">" : (const char *)" ", (UINT64)(i + 1), m[i].name);
    if (left >= 0)
        console_printf("\r\n%s starts in %u s. Up and Down choose, Enter starts.\r\n", m[sel].name, (UINT64)left);
    else
        console_printf("\r\nUp and Down choose, Enter starts.\r\n");
}

/* The menu itself: the line chosen */
static int menu(EFI_SYSTEM_TABLE *st, RecInput *in, int failed)
{
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *out = st->ConOut;
    STALL stall = (STALL)g_rbs->Stall;
    Line m[4];
    int n = 0;
    m[n++] = (Line){ "Start NovaOS normally", RECOVERY_NORMAL };
    if (exists(OLD_KERNEL_PATH)) m[n++] = (Line){ "Start the previous version of NovaOS", RECOVERY_PREVIOUS };
    m[n++] = (Line){ "Safe mode", RECOVERY_SAFE_MODE };
    m[n++] = (Line){ "Startup repair", RECOVERY_REPAIR };
    int sel = failed ? n - 1 : 0, left = failed ? RECOVERY_SECONDS : -1, ticks = 0;
    console_printf("Advanced startup%s\r\n", failed ? (const char *)" (after failed starts)" : (const char *)"");
    draw(out, m, n, sel, left, failed);
    for (;;) {
        RecKey k;
        if (in && !EFI_ERROR(in->ReadKeyStroke(in, &k))) {
            int was = sel;
            if (k.ScanCode == SCAN_UP) sel = sel ? sel - 1 : n - 1;
            else if (k.ScanCode == SCAN_DOWN) sel = (sel + 1) % n;
            else if (k.UnicodeChar >= '1' && k.UnicodeChar < '1' + n) sel = k.UnicodeChar - '1';
            else if (k.UnicodeChar == '\r' || k.UnicodeChar == '\n') break;
            else if (k.ScanCode == SCAN_F8) continue;          /* (F8 still held from asking for the menu) */
            if (sel != was || left >= 0) {
                left = -1;                                     /* (a key stops the countdown) */
                draw(out, m, n, sel, left, failed);
            }
            continue;
        }
        stall(50000);
        if (left < 0) continue;
        if (++ticks % 20) continue;
        if (--left <= 0) break;
        draw(out, m, n, sel, left, failed);
    }
    out->ClearScreen(out);
    console_printf("Advanced startup: %s\r\n", m[sel].name);
    return m[sel].choice;
}

int recovery_start(EFI_HANDLE image, EFI_SYSTEM_TABLE *st, EFI_HANDLE dev, UINT64 *flags)
{
    g_rbs = st->BootServices;
    g_rimage = image;
    g_rdev = dev;
    *flags = 0;
    if (started_as_previous()) {
        console_printf("Started as the previous version's boot loader: starting kernel.old\r\n");
        *flags = BOOT_FLAG_PREVIOUS;
        return RECOVERY_PREVIOUS;
    }

    /* (a volume that also keeps drive C:, \NOVA\C, is written by the
     * kernel's drive C: alone: no count there) */
    BOOLEAN counting = !exists(DRIVE_C_PATH);
    int count = counting ? read_count() : 0;
    BOOLEAN asked = take_request(), f8 = FALSE;
    RecInput *in = (RecInput *)st->ConIn;
    STALL stall = (STALL)g_rbs->Stall;
    for (int t = 0; in && !asked && count < BOOT_FAILED_STARTS && t * 10 < KEY_WINDOW_MS && !f8; t++) {
        RecKey k;
        while (!f8 && !EFI_ERROR(in->ReadKeyStroke(in, &k))) f8 = k.ScanCode == SCAN_F8;
        if (!f8) stall(10000);
    }
    int choice = RECOVERY_NORMAL;
    if (asked || f8 || count >= BOOT_FAILED_STARTS) {
        if (count >= BOOT_FAILED_STARTS) {
            console_printf("The last %u starts did not reach the desktop\r\n", (UINT64)count);
            *flags |= BOOT_FLAG_FAILED_STARTS;
        }
        choice = menu(st, in, count >= BOOT_FAILED_STARTS ? count : 0);
    }
    if (counting) write_count(count + 1);                      /* (the kernel deletes it at the desktop) */
    switch (choice) {
    case RECOVERY_PREVIOUS:
        *flags |= BOOT_FLAG_PREVIOUS;
        if (exists(OLD_LOADER_PATH)) start_previous_loader();  /* (comes back only if it could not start) */
        break;
    case RECOVERY_SAFE_MODE: *flags |= BOOT_FLAG_SAFE_MODE; break;
    case RECOVERY_REPAIR:    *flags |= BOOT_FLAG_STARTUP_REPAIR; break;
    }
    return choice;
}
