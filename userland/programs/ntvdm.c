/* ntvdm.exe — NovaOS's DOS host, after Windows' NT Virtual DOS Machine:
 * MS-DOS 4.00 (built from Microsoft's MIT-licensed source, docs/dos.md)
 * on a real-mode CPU and PC BIOS (ntvdm/cpu86.c, ntvdm/pcbios.c) in this
 * process, its text screen in this console window.
 *
 *     ntvdm [IMAGE]
 *     ntvdm -run PROGRAM [ARGUMENTS]
 *
 * boots IMAGE, a floppy disk image (default: DOS\MSDOS.IMG beside
 * ntvdm.exe, the MS-DOS 4.00 boot disk the build makes), as drive A:.
 * What DOS writes to the disk stays in memory: the image file is not
 * changed.  Drive C: in DOS is NovaOS's (ntvdm/redir.h), with this
 * process's current directory as DOS's there.  The screen is drawn on the
 * console's alternate screen, so the Terminal's own text comes back when
 * DOS ends; F12 ends it (the shell DOS boots to has no EXIT, as on a PC).
 *
 * -run is how NovaOS starts a DOS program, as Windows started ntvdm.exe
 * for one: CreateProcess, the Terminal and Explorer, given a 16-bit .COM
 * or .EXE, start "ntvdm.exe -run PROGRAM ARGUMENTS" instead (the kernel's
 * process start does it, kernel/um/um.c).  REDIR.COM runs PROGRAM (a path
 * on C:) with ARGUMENTS as its command tail; when it ends, so does
 * ntvdm.exe, with its exit code, and what it left on the screen stays in
 * the console. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "ntvdm/pcbios.h"

BOOL WINAPI SetConsoleCursorPosition(HANDLE h, COORD at);
WINBASEAPI BOOL WINAPI FileTimeToDosDateTime(const FILETIME *ft, LPWORD date, LPWORD time);
typedef struct { DWORD dwSize; BOOL bVisible; } CURSOR_INFO;
BOOL WINAPI SetConsoleCursorInfo(HANDLE h, const CURSOR_INFO *info);
BOOL WINAPI GetConsoleCursorInfo(HANDLE h, CURSOR_INFO *info);

#define KEY_EVENT 0x0001
typedef struct {
    WORD EventType;
    union {
        struct { BOOL bKeyDown; WORD wRepeatCount, wVirtualKeyCode, wVirtualScanCode;
                 union { WCHAR UnicodeChar; CHAR AsciiChar; } uChar; DWORD dwControlKeyState; } KeyEvent;
        BYTE pad[16];
    } Event;
} INPUT_RECORD;
WINBASEAPI BOOL WINAPI ReadConsoleInputW(HANDLE h, INPUT_RECORD *rec, DWORD n, LPDWORD read);

#define RIGHT_ALT 0x0001
#define LEFT_ALT 0x0002
#define RIGHT_CTRL 0x0004
#define LEFT_CTRL 0x0008
#define SHIFT 0x0010
#define NUMLOCK 0x0020
#define SCROLLLOCK 0x0040
#define CAPSLOCK 0x0080
#define ENHANCED 0x0100

/* code page 437, the PC's characters, as Unicode */
static const WCHAR cp437_low[32] =
    L" \x263A\x263B\x2665\x2666\x2663\x2660\x2022\x25D8\x25CB\x25D9\x2642\x2640\x266A\x266B\x263C"
    L"\x25BA\x25C4\x2195\x203C\x00B6\x00A7\x25AC\x21A8\x2191\x2193\x2192\x2190\x221F\x2194\x25B2\x25BC";
static const WCHAR cp437_high[128] =
    L"\x00C7\x00FC\x00E9\x00E2\x00E4\x00E0\x00E5\x00E7\x00EA\x00EB\x00E8\x00EF\x00EE\x00EC\x00C4\x00C5"
    L"\x00C9\x00E6\x00C6\x00F4\x00F6\x00F2\x00FB\x00F9\x00FF\x00D6\x00DC\x00A2\x00A3\x00A5\x20A7\x0192"
    L"\x00E1\x00ED\x00F3\x00FA\x00F1\x00D1\x00AA\x00BA\x00BF\x2310\x00AC\x00BD\x00BC\x00A1\x00AB\x00BB"
    L"\x2591\x2592\x2593\x2502\x2524\x2561\x2562\x2556\x2555\x2563\x2551\x2557\x255D\x255C\x255B\x2510"
    L"\x2514\x2534\x252C\x251C\x2500\x253C\x255E\x255F\x255A\x2554\x2569\x2566\x2560\x2550\x256C\x2567"
    L"\x2568\x2564\x2565\x2559\x2558\x2552\x2553\x256B\x256A\x2518\x250C\x2588\x2584\x258C\x2590\x2580"
    L"\x03B1\x00DF\x0393\x03C0\x03A3\x03C3\x00B5\x03C4\x03A6\x0398\x03A9\x03B4\x221E\x03C6\x03B5\x2229"
    L"\x2261\x00B1\x2265\x2264\x2320\x2321\x00F7\x2248\x00B0\x2219\x00B7\x221A\x207F\x00B2\x25A0\x00A0";

static WCHAR cp437(uint8_t ch)
{
    if (ch < 32) return cp437_low[ch];
    if (ch == 0x7F) return 0x2302;
    if (ch >= 0x80) return cp437_high[ch - 0x80];
    return ch;
}

static int to_cp437(WCHAR w)
{
    if (w < 0x80) return w;
    for (int i = 0; i < 128; i++) if (cp437_high[i] == w) return 0x80 + i;
    return -1;
}

/* ---- drive C: (ntvdm/redir.h): NovaOS's C:\ ---- */

static void host_path(const char *path, WCHAR *out)
{
    char full[VDM_PATH + 4] = "C:\\";
    strncat(full, path, VDM_PATH - 1);
    if (!MultiByteToWideChar(CP_UTF8, 0, full, -1, out, VDM_PATH + 4)) out[0] = 0;
}

static int dos_error(void)
{
    DWORD e = GetLastError();
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND || e == ERROR_TOO_MANY_OPEN_FILES) return (int)e;
    return e == ERROR_FILE_EXISTS || e == ERROR_ALREADY_EXISTS ? 0x50 : 5;
}

static void file_info(const WCHAR *name, DWORD attrs, FILETIME mtime, DWORD size, VdmFile *f)
{
    memset(f, 0, sizeof(*f));
    WideCharToMultiByte(CP_UTF8, 0, name, -1, f->name, (int)sizeof(f->name), NULL, NULL);
    f->attr = (uint8_t)(attrs & 0x37);          /* (read-only, hidden, system, directory, archive: DOS's bits) */
    f->size = attrs & FILE_ATTRIBUTE_DIRECTORY ? 0 : size;
    FILETIME local;
    WORD date = 0, time = 0;
    if (FileTimeToLocalFileTime(&mtime, &local) && FileTimeToDosDateTime(&local, &date, &time)) {
        f->date = date;
        f->time = time;
    }
}

static int fs_list(void *ctx, const char *dir, int (*fn)(void *arg, const VdmFile *f), void *arg)
{
    (void)ctx;
    WCHAR pat[VDM_PATH + 8];
    host_path(dir, pat);
    size_t n = wcslen(pat);
    if (n && pat[n - 1] != L'\\') pat[n++] = L'\\';
    pat[n++] = L'*';
    pat[n] = 0;
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW(pat, &d);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : 3;
    do {
        if (!wcscmp(d.cFileName, L".") || !wcscmp(d.cFileName, L"..")) continue;
        VdmFile f;
        file_info(d.cFileName, d.dwFileAttributes, d.ftLastWriteTime, d.nFileSizeHigh ? 0xFFFFFFFF : d.nFileSizeLow, &f);
        if (fn(arg, &f)) break;
    } while (FindNextFileW(h, &d));
    FindClose(h);
    return 0;
}

static int fs_stat(void *ctx, const char *path, VdmFile *f)
{
    (void)ctx;
    WCHAR p[VDM_PATH + 4];
    WIN32_FILE_ATTRIBUTE_DATA a;
    host_path(path, p);
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &a)) return dos_error();
    const WCHAR *base = wcsrchr(p, L'\\');
    file_info(base ? base + 1 : p, a.dwFileAttributes, a.ftLastWriteTime, a.nFileSizeHigh ? 0xFFFFFFFF : a.nFileSizeLow, f);
    return 0;
}

static int fs_open(void *ctx, const char *path, int how, void **file)
{
    (void)ctx;
    WCHAR p[VDM_PATH + 4];
    host_path(path, p);
    DWORD access = (how & 3) == VDMFS_READ ? GENERIC_READ : (how & 3) == VDMFS_WRITE ? GENERIC_WRITE
                                                                                   : GENERIC_READ | GENERIC_WRITE;
    DWORD disp = how & VDMFS_NEW ? CREATE_NEW : how & VDMFS_CREATE ? CREATE_ALWAYS : OPEN_EXISTING;
    HANDLE h = CreateFileW(p, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, disp,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return dos_error();
    *file = h;
    return 0;
}

static void fs_close(void *ctx, void *file) { (void)ctx; CloseHandle(file); }

static int fs_read(void *ctx, void *file, uint32_t pos, void *buf, uint32_t n, uint32_t *got)
{
    (void)ctx;
    DWORD k = 0;
    if (SetFilePointer(file, (LONG)pos, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER || !ReadFile(file, buf, n, &k, NULL))
        return 5;
    *got = k;
    return 0;
}

static int fs_write(void *ctx, void *file, uint32_t pos, const void *buf, uint32_t n)
{
    (void)ctx;
    DWORD k = 0;
    if (SetFilePointer(file, (LONG)pos, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return 5;
    if (!n) return SetEndOfFile(file) ? 0 : 5;
    return WriteFile(file, buf, n, &k, NULL) && k == n ? 0 : 5;
}

static int fs_remove(void *ctx, const char *path)
{
    (void)ctx;
    WCHAR p[VDM_PATH + 4];
    host_path(path, p);
    return DeleteFileW(p) ? 0 : dos_error();
}

static int fs_rename(void *ctx, const char *from, const char *to)
{
    (void)ctx;
    WCHAR a[VDM_PATH + 4], b[VDM_PATH + 4];
    host_path(from, a);
    host_path(to, b);
    return MoveFileW(a, b) ? 0 : dos_error();
}

static int fs_mkdir(void *ctx, const char *path)
{
    (void)ctx;
    WCHAR p[VDM_PATH + 4];
    host_path(path, p);
    return CreateDirectoryW(p, NULL) ? 0 : dos_error();
}

static int fs_rmdir(void *ctx, const char *path)
{
    (void)ctx;
    WCHAR p[VDM_PATH + 4];
    host_path(path, p);
    return RemoveDirectoryW(p) ? 0 : dos_error();
}

static void fs_space(void *ctx, uint64_t *total, uint64_t *avail)
{
    (void)ctx;
    ULARGE_INTEGER a, t, f;
    if (!GetDiskFreeSpaceExW(L"C:\\", &a, &t, &f)) { *total = *avail = 0; return; }
    *total = t.QuadPart;
    *avail = a.QuadPart;
}

static const VdmFs drive_c = { NULL, fs_list, fs_stat, fs_open, fs_close, fs_read, fs_write,
                               fs_remove, fs_rename, fs_mkdir, fs_rmdir, fs_space };

/* a path on drive C: as one under its root ("C:\Users\x" is "Users\x");
 * NULL if it is on another drive */
static const char *on_c(const char *path)
{
    if ((path[0] | 0x20) != 'c' || path[1] != ':') return NULL;
    path += 2;
    while (*path == '\\' || *path == '/') path++;
    return path;
}

/* ---- the command line: ntvdm -run PROGRAM ARGUMENTS ---- */

static const char *skip_arg(const char *s, char *out, int cap)
{
    int n = 0, q = 0;
    while (*s == ' ' || *s == '\t') s++;
    for (; *s && (q || (*s != ' ' && *s != '\t')); s++) {
        if (*s == '"') { q = !q; continue; }
        if (out && n < cap - 1) out[n++] = *s;
    }
    if (out) out[n] = 0;
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static uint64_t host_ms(void *ctx) { (void)ctx; return GetTickCount64(); }

static void host_localtime(void *ctx, int t[6])
{
    (void)ctx;
    SYSTEMTIME st;
    GetLocalTime(&st);
    t[0] = st.wYear; t[1] = st.wMonth; t[2] = st.wDay;
    t[3] = st.wHour; t[4] = st.wMinute; t[5] = st.wSecond;
}

/* COM1: a line at a time into the debug log */
static char com1[256];
static int com1_len;
static void host_serial(void *ctx, uint8_t ch)
{
    (void)ctx;
    if (ch == '\n' || com1_len == (int)sizeof(com1) - 2) {
        com1[com1_len++] = '\n';
        com1[com1_len] = 0;
        OutputDebugStringA(com1);
        com1_len = 0;
    } else if (ch != '\r') com1[com1_len++] = (char)ch;
}

/* ---- the screen ---- */

static HANDLE out, in;
static int shown_cols, shown_rows;
static uint8_t shown[80 * 50 * 2];
static int shown_x = -1, shown_y = -1, shown_cursor = -1, drawn;

static void draw(Vdm *v)
{
    int cols, rows, x, y, visible;
    const uint8_t *t = vdm_text(v, &cols, &rows);
    if (cols > 80) cols = 80;
    if (rows > 50) rows = 50;
    size_t n = (size_t)cols * rows * 2;
    CONSOLE_SCREEN_BUFFER_INFO info;            /* (the window grew to fit 25 rows: all of it again) */
    static COORD shown_size;
    if (GetConsoleScreenBufferInfo(out, &info) &&
        (info.dwSize.X != shown_size.X || info.dwSize.Y != shown_size.Y)) {
        shown_size = info.dwSize;
        drawn = 0;
    }
    int top = 0, bottom = rows - 1;             /* (the rows that changed: the console redraws them) */
    if (drawn && cols == shown_cols && rows == shown_rows) {
        size_t row = (size_t)cols * 2;
        while (top < rows && !memcmp(t + top * row, shown + top * row, row)) top++;
        while (bottom > top && !memcmp(t + bottom * row, shown + bottom * row, row)) bottom--;
    }
    if (top < rows) {
        static CHAR_INFO cells[80 * 50];
        for (int i = top * cols; i < (bottom + 1) * cols; i++) {
            cells[i].Char.UnicodeChar = cp437(t[i * 2]);
            cells[i].Attributes = t[i * 2 + 1];  /* (CGA's colour bits are the console's) */
        }
        SMALL_RECT r = { 0, (SHORT)top, (SHORT)(cols - 1), (SHORT)bottom };
        WriteConsoleOutputW(out, cells, (COORD){ (SHORT)cols, (SHORT)rows }, (COORD){ 0, (SHORT)top }, &r);
        memcpy(shown, t, n);
        shown_cols = cols;
        shown_rows = rows;
        drawn = 1;
    }
    vdm_cursor(v, &x, &y, &visible);
    if (x != shown_x || y != shown_y) {
        SetConsoleCursorPosition(out, (COORD){ (SHORT)x, (SHORT)y });
        shown_x = x;
        shown_y = y;
    }
    if (visible != shown_cursor) {
        CURSOR_INFO ci = { 25, visible };
        SetConsoleCursorInfo(out, &ci);
        shown_cursor = visible;
    }
}

/* ---- the keyboard ---- */

/* the BIOS's key for a key the console reports without a character */
static uint16_t special_key(WORD vk, DWORD ctrl)
{
    int shift = (ctrl & SHIFT) != 0, ctl = (ctrl & (LEFT_CTRL | RIGHT_CTRL)) != 0;
    int alt = (ctrl & (LEFT_ALT | RIGHT_ALT)) != 0;
    uint16_t pad = (ctrl & ENHANCED) ? 0xE0 : 0;
    if (vk >= VK_F1 && vk <= VK_F10) {
        int base = alt ? 0x68 : ctl ? 0x5E : shift ? 0x54 : 0x3B;
        return (uint16_t)((base + vk - VK_F1) << 8);
    }
    switch (vk) {
    case VK_F11: return alt ? 0x8B00 : ctl ? 0x8900 : shift ? 0x8700 : 0x8500;
    case VK_UP: return (uint16_t)((ctl ? 0x8D : 0x48) << 8 | pad);
    case VK_DOWN: return (uint16_t)((ctl ? 0x91 : 0x50) << 8 | pad);
    case VK_LEFT: return (uint16_t)((ctl ? 0x73 : 0x4B) << 8 | pad);
    case VK_RIGHT: return (uint16_t)((ctl ? 0x74 : 0x4D) << 8 | pad);
    case VK_HOME: return (uint16_t)((ctl ? 0x77 : 0x47) << 8 | pad);
    case VK_END: return (uint16_t)((ctl ? 0x75 : 0x4F) << 8 | pad);
    case VK_PRIOR: return (uint16_t)((ctl ? 0x84 : 0x49) << 8 | pad);
    case VK_NEXT: return (uint16_t)((ctl ? 0x76 : 0x51) << 8 | pad);
    case VK_INSERT: return (uint16_t)(0x52 << 8 | pad);
    case VK_DELETE: return (uint16_t)(0x53 << 8 | pad);
    case VK_TAB: return shift ? 0x0F00 : 0x0F09;
    case VK_BACK: return 0x0E08;
    case VK_RETURN: return 0x1C0D;
    case VK_ESCAPE: return 0x011B;
    }
    return 0;
}

/* the console's keys into the BIOS's buffer; 0 when F12 asks to leave */
static int keys(Vdm *v)
{
    DWORD n = 0;
    while (GetNumberOfConsoleInputEvents(in, &n) && n) {
        INPUT_RECORD rec[16];
        if (!ReadConsoleInputW(in, rec, 16, &n) || !n) break;
        for (DWORD i = 0; i < n; i++) {
            if (rec[i].EventType != KEY_EVENT) continue;
            DWORD ctrl = rec[i].Event.KeyEvent.dwControlKeyState;
            uint8_t sh = (uint8_t)(((ctrl & SHIFT) ? 2 : 0) | ((ctrl & (LEFT_CTRL | RIGHT_CTRL)) ? 4 : 0) |
                                   ((ctrl & (LEFT_ALT | RIGHT_ALT)) ? 8 : 0) | ((ctrl & SCROLLLOCK) ? 0x10 : 0) |
                                   ((ctrl & NUMLOCK) ? 0x20 : 0) | ((ctrl & CAPSLOCK) ? 0x40 : 0));
            vdm_shift_state(v, sh);
            if (!rec[i].Event.KeyEvent.bKeyDown) continue;
            WORD vk = rec[i].Event.KeyEvent.wVirtualKeyCode;
            WCHAR ch = rec[i].Event.KeyEvent.uChar.UnicodeChar;
            if (vk == VK_F12) return 0;
            uint16_t key = 0;
            int c437;
            if (ch && vk != VK_BACK && vk != VK_TAB && vk != VK_RETURN && vk != VK_ESCAPE &&
                (c437 = to_cp437(ch)) >= 0) {
                key = vdm_char_key((uint8_t)c437);
                if (ch >= 'a' && ch <= 'z' && (ctrl & (LEFT_CTRL | RIGHT_CTRL)))
                    key = vdm_char_key((uint8_t)(ch - 'a' + 1));
                if (ctrl & (LEFT_ALT | RIGHT_ALT)) key &= 0xFF00;           /* (Alt+key: no character) */
            } else key = special_key(vk, ctrl);
            for (WORD r = 0; key && r < (rec[i].Event.KeyEvent.wRepeatCount ? rec[i].Event.KeyEvent.wRepeatCount : 1); r++)
                vdm_key(v, key);
        }
    }
    return 1;
}

static int image_path(int argc, char **argv, WCHAR *path, DWORD cap)
{
    if (argc > 1 && strcmp(argv[1], "-run")) return MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path, (int)cap) > 0;
    DWORD n = GetModuleFileNameW(NULL, path, cap);
    while (n && path[n - 1] != L'\\') n--;
    static const WCHAR rel[] = L"DOS\\MSDOS.IMG";
    if (n + sizeof(rel) / sizeof(WCHAR) > cap) return 0;
    memcpy(path + n, rel, sizeof(rel));
    return 1;
}

int main(int argc, char **argv)
{
    WCHAR path[MAX_PATH];
    if (!image_path(argc, argv, path, MAX_PATH)) return 2;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        printf("ntvdm: cannot open %ls (error %lu)\n", path, GetLastError());
        return 2;
    }
    DWORD size = GetFileSize(f, NULL), got = 0;
    uint8_t *disk = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    uint8_t *mem = HeapAlloc(GetProcessHeap(), 0, CPU86_MEM_SIZE);
    static Vdm vdm;
    BOOL ok = disk && mem && ReadFile(f, disk, size, &got, NULL) && got == size;
    CloseHandle(f);
    VdmHost host = { NULL, host_ms, host_localtime, host_serial };
    if (!ok || !vdm_init(&vdm, mem, disk, size, &host)) {
        printf("ntvdm: %ls is not a floppy disk image\n", path);
        return 2;
    }

    char cwd[MAX_PATH * 3], program[MAX_PATH * 3] = "";
    const char *args = "";
    if (!GetCurrentDirectoryA(sizeof(cwd), cwd)) cwd[0] = 0;
    vdm_drive_c(&vdm, &drive_c, on_c(cwd) ? on_c(cwd) : "");
    if (argc > 1 && !strcmp(argv[1], "-run")) {
        args = skip_arg(skip_arg(skip_arg(GetCommandLineA(), NULL, 0), NULL, 0), program, (int)sizeof(program));
        if (!on_c(program) || !vdm_run_program(&vdm, on_c(program), args)) {
            printf("ntvdm: DOS sees drive C: only, by 8.3 names; it cannot run %s\n", program);
            return 1;
        }
    }

    out = GetStdHandle(STD_OUTPUT_HANDLE);
    in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD in_mode = 0, out_mode = 0, n;
    GetConsoleMode(in, &in_mode);
    GetConsoleMode(out, &out_mode);
    SetConsoleMode(in, 0);                      /* (keys as they come: Ctrl+C is DOS's) */
    SetConsoleMode(out, out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    CURSOR_INFO old_cursor = { 25, TRUE };
    GetConsoleCursorInfo(out, &old_cursor);
    WriteConsoleA(out, "\x1b[?1049h\x1b[H\x1b[2J", 16, &n, NULL);   /* the alternate screen */
    SetConsoleTitleW(program[0] ? L"MS-DOS 4.00" : L"MS-DOS 4.00 (F12 returns to NovaOS)");
    OutputDebugStringA("ntvdm: booting MS-DOS\n");

    uint64_t drawn_at = 0;
    for (;;) {
        int r = vdm_run(&vdm, 100000);
        if (r == VDM_ENDED) break;
        uint64_t now = GetTickCount64();
        if (r != VDM_RAN || now - drawn_at >= 40) {
            draw(&vdm);
            drawn_at = now;
        }
        if (!keys(&vdm)) break;
        if (r == VDM_RAN) continue;
        /* DOS waits: sleep until a key or the next timer tick */
        double next = vdm_next_tick(&vdm);
        DWORD ms = r == VDM_IDLE ? 5 : next > (double)now ? (DWORD)(next - (double)now) + 1 : 1;
        if (ms > 20) ms = 20;
        Sleep(ms);
    }

    WriteConsoleA(out, "\x1b[?1049l", 8, &n, NULL);
    SetConsoleCursorInfo(out, &old_cursor);
    SetConsoleMode(in, in_mode);
    SetConsoleMode(out, out_mode);
    if (!vdm.redir.ended) {
        OutputDebugStringA("ntvdm: MS-DOS ended (F12)\n");
        return 0;
    }
    /* the program ended: what it left on the screen stays in the console,
     * as when it ran in Windows' console window */
    int cols, rows, cx, cy, vis;
    const uint8_t *t = vdm_text(&vdm, &cols, &rows);
    vdm_cursor(&vdm, &cx, &cy, &vis);
    int last = cx ? cy : cy - 1;
    while (last >= 0) {
        int blank = 1;
        for (int x = 0; x < cols && blank; x++) blank = t[(last * cols + x) * 2] == ' ' || !t[(last * cols + x) * 2];
        if (!blank) break;
        last--;
    }
    for (int y = 0; y <= last && y < rows; y++) {
        WCHAR line[82];
        int len = 0;
        for (int x = 0; x < cols && x < 80; x++) line[len++] = cp437(t[(y * cols + x) * 2]);
        while (len && line[len - 1] == L' ') len--;
        line[len++] = L'\r';
        line[len++] = L'\n';
        WriteConsoleW(out, line, (DWORD)len, &n, NULL);
    }
    char msg[MAX_PATH * 3 + 64];
    if (vdm.redir.exec_error) {
        printf("ntvdm: DOS could not start %s (error %d)\n", program, vdm.redir.exec_error);
        return 1;
    }
    snprintf(msg, sizeof(msg), "ntvdm: %s ended with exit code %d\n", program, vdm.redir.exit_code);
    OutputDebugStringA(msg);
    return vdm.redir.exit_code;
}
