/*
 * bootmenu.c — NovaOS's boot menu: the other systems on the PC
 *
 * On an installed disk the boot loader looks on every other volume the
 * firmware can read for another system's boot manager, as Windows' Boot
 * Manager lists the other systems: today "Windows Boot Manager",
 * \EFI\Microsoft\Boot\bootmgfw.efi on Windows' EFI System Partition
 * (docs/design/installer.md).  When there is one, a menu shows NovaOS and
 * it for five seconds; NovaOS starts unless another line is chosen (the
 * arrow keys or its number, then Enter).  A chosen system is started with
 * LoadImage/StartImage as the firmware's boot manager would; if it comes
 * back (or cannot be started) the menu shows again.
 *
 * Text only, through the firmware's console (which QEMU's firmware also
 * copies to the serial port: the boot tests read it there).
 */

#include "../include/efi.h"

void console_printf(const char *fmt, ...);

#define MENU_SECONDS 5
#define MENU_MAX     4

typedef struct { UINT16 ScanCode; CHAR16 UnicodeChar; } EFI_INPUT_KEY;
typedef struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_STATUS (__attribute__((ms_abi)) *Reset)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *self, BOOLEAN ext);
    EFI_STATUS (__attribute__((ms_abi)) *ReadKeyStroke)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *self, EFI_INPUT_KEY *key);
    VOID *WaitForKey;
};
#define SCAN_UP   0x01
#define SCAN_DOWN 0x02

typedef EFI_STATUS (__attribute__((ms_abi)) *LOCATE_HANDLE_BUFFER)(UINTN type, EFI_GUID *protocol, VOID *key,
                                                                    UINTN *count, EFI_HANDLE **handles);
typedef EFI_STATUS (__attribute__((ms_abi)) *LOAD_IMAGE)(BOOLEAN boot_policy, EFI_HANDLE parent, VOID *path,
                                                         VOID *source, UINTN size, EFI_HANDLE *image);
typedef EFI_STATUS (__attribute__((ms_abi)) *START_IMAGE)(EFI_HANDLE image, UINTN *exit_size, CHAR16 **exit_data);
typedef EFI_STATUS (__attribute__((ms_abi)) *STALL)(UINTN us);
typedef EFI_STATUS (__attribute__((ms_abi)) *SET_WATCHDOG)(UINTN seconds, UINT64 code, UINTN size, CHAR16 *data);
#define BY_PROTOCOL 2

#define DEVICE_PATH_GUID \
    { 0x09576e91, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

typedef struct {
    const char *name;
    EFI_HANDLE  volume;                    /* NULL: NovaOS itself */
    CHAR16     *file;
} MenuEntry;

static CHAR16 BOOTMGFW[] = { '\\','E','F','I','\\','M','i','c','r','o','s','o','f','t','\\','B','o','o','t',
                             '\\','b','o','o','t','m','g','f','w','.','e','f','i', 0 };

static UINTN wlen(const CHAR16 *s) { UINTN n = 0; while (s[n]) n++; return n; }

/* Whether @file exists on volume @h */
static BOOLEAN volume_has(EFI_BOOT_SERVICES *bs, EFI_HANDLE image, EFI_HANDLE h, CHAR16 *file)
{
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE_PROTOCOL *root, *f;
    if (EFI_ERROR(bs->OpenProtocol(h, &fs_guid, (VOID **)&fs, image, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL)) ||
        EFI_ERROR(fs->OpenVolume(fs, &root)))
        return FALSE;
    EFI_STATUS st = root->Open(root, &f, file, EFI_FILE_MODE_READ, 0);
    root->Close(root);
    if (EFI_ERROR(st)) return FALSE;
    f->Close(f);
    return TRUE;
}

/* Start @file on volume @h: its device path and the file's, as a boot
 * entry's; returns when it comes back */
static void start_other(EFI_BOOT_SERVICES *bs, EFI_HANDLE image, const MenuEntry *e)
{
    EFI_GUID dp_guid = DEVICE_PATH_GUID;
    UINT8 *dp = NULL;
    if (EFI_ERROR(bs->OpenProtocol(e->volume, &dp_guid, (VOID **)&dp, image, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL)) || !dp) {
        console_printf("%s: its volume has no device path\r\n", e->name);
        return;
    }
    UINTN n = 0;
    for (int k = 0; k < 64; k++) {                       /* up to the end node */
        UINT16 len = (UINT16)(dp[n + 2] | dp[n + 3] << 8);
        if (dp[n] == 0x7F || len < 4) break;
        n += len;
    }
    UINTN flen = 4 + 2 * (wlen(e->file) + 1);
    UINT8 *path = NULL;
    if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, n + flen + 4, (VOID **)&path))) return;
    for (UINTN i = 0; i < n; i++) path[i] = dp[i];
    UINT8 *fp = path + n;
    fp[0] = 4; fp[1] = 4; fp[2] = (UINT8)flen; fp[3] = (UINT8)(flen >> 8);
    for (UINTN i = 0; i <= wlen(e->file); i++) { fp[4 + 2 * i] = (UINT8)e->file[i]; fp[5 + 2 * i] = (UINT8)(e->file[i] >> 8); }
    UINT8 *end = fp + flen;
    end[0] = 0x7F; end[1] = 0xFF; end[2] = 4; end[3] = 0;

    console_printf("Starting %s...\r\n", e->name);
    EFI_HANDLE img = NULL;
    EFI_STATUS st = ((LOAD_IMAGE)bs->LoadImage)(FALSE, image, path, NULL, 0, &img);
    bs->FreePool(path);
    if (EFI_ERROR(st)) {
        console_printf("%s could not be started (LoadImage: status %x)\r\n", e->name, (UINT64)st);
        return;
    }
    /* (the firmware's watchdog, as for a boot entry: the boot manager stops it) */
    ((SET_WATCHDOG)bs->SetWatchdogTimer)(300, 0, 0, NULL);
    st = ((START_IMAGE)bs->StartImage)(img, NULL, NULL);
    ((SET_WATCHDOG)bs->SetWatchdogTimer)(0, 0, 0, NULL);
    console_printf("%s returned (status %x)\r\n", e->name, (UINT64)st);
}

static void draw(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *out, const MenuEntry *m, int n, int sel, int left)
{
    out->ClearScreen(out);
    console_printf("NovaOS boot menu\r\n\r\nChoose a system to start:\r\n\r\n");
    for (int i = 0; i < n; i++)
        console_printf("  %s %u. %s\r\n", i == sel ? (const char *)">" : (const char *)" ", (UINT64)(i + 1), m[i].name);
    if (left >= 0)
        console_printf("\r\n%s starts in %u s. Up and Down choose, Enter starts.\r\n", m[sel].name, (UINT64)left);
    else
        console_printf("\r\nUp and Down choose, Enter starts.\r\n");
}

void boot_menu(EFI_HANDLE image, EFI_SYSTEM_TABLE *st, EFI_HANDLE self)
{
    EFI_BOOT_SERVICES *bs = st->BootServices;
    MenuEntry m[MENU_MAX];
    int n = 0;
    m[n++] = (MenuEntry){ "NovaOS", NULL, NULL };

    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_HANDLE *hs = NULL;
    UINTN count = 0;
    if (!EFI_ERROR(((LOCATE_HANDLE_BUFFER)bs->LocateHandleBuffer2)(BY_PROTOCOL, &fs_guid, NULL, &count, &hs))) {
        for (UINTN i = 0; i < count && n < MENU_MAX; i++)
            if (hs[i] != self && volume_has(bs, image, hs[i], BOOTMGFW)) {
                m[n++] = (MenuEntry){ "Windows Boot Manager", hs[i], BOOTMGFW };
                break;                              /* (one Windows Boot Manager lists every Windows) */
            }
        bs->FreePool(hs);
    }
    if (n < 2) return;
    console_printf("Other systems on this PC: %s\r\n", m[1].name);

    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *in = (EFI_SIMPLE_TEXT_INPUT_PROTOCOL *)st->ConIn;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *out = st->ConOut;
    STALL stall = (STALL)bs->Stall;
    if (in) in->Reset(in, FALSE);
    int sel = 0, left = MENU_SECONDS, ticks = 0;
    draw(out, m, n, sel, left);
    for (;;) {
        EFI_INPUT_KEY k;
        if (in && !EFI_ERROR(in->ReadKeyStroke(in, &k))) {
            int was = sel;
            BOOLEAN go = FALSE;
            if (k.ScanCode == SCAN_UP) sel = sel ? sel - 1 : n - 1;
            else if (k.ScanCode == SCAN_DOWN) sel = (sel + 1) % n;
            else if (k.UnicodeChar >= '1' && k.UnicodeChar < '1' + n) sel = k.UnicodeChar - '1';
            else if (k.UnicodeChar == '\r' || k.UnicodeChar == '\n') go = TRUE;
            left = -1;                               /* (a key stops the countdown) */
            if (go) {
                if (!m[sel].volume) break;
                start_other(bs, image, &m[sel]);
                console_printf("\r\n");
                stall(2000000);                      /* (its last words stay readable) */
            }
            if (go || sel != was || ticks >= 0) draw(out, m, n, sel, left);
            ticks = -1;
            continue;
        }
        stall(50000);
        if (left < 0) continue;
        if (++ticks % 20) continue;
        if (--left <= 0) break;
        draw(out, m, n, sel, left);
    }
    out->ClearScreen(out);
    console_printf("Starting NovaOS\r\n");
}
