/* A full-screen program drawn in a console window and through a pseudo
 * console: the child draws with VT sequences and the classic console calls
 * together (alternate screen, colours, wide characters, erasing, scroll
 * regions, inserted and deleted lines, wrapping, cell writes, fills and
 * scrolling) and saves the cells it reads back.  It runs twice, in this
 * console and under CreatePseudoConsole at the same size; the pseudo
 * console's output, written into a screen buffer of this console, must
 * give the same cells, and both runs must read back the same screen. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

BOOL WINAPI SetConsoleCursorPosition(HANDLE h, COORD at);

static int passed, failed;
#define CHECK(name, ok) do { if (ok) passed++; else { failed++; printf("FAIL: %s\n", name); } } while (0)

typedef struct {
    SHORT cols, rows, cursor_x, cursor_y;
    CHAR_INFO cells[1];
} Snapshot;

static void put(HANDLE out, const char *s)
{
    DWORD n;
    WriteConsoleA(out, s, (DWORD)strlen(s), &n, 0);
}

static void putw(HANDLE out, const WCHAR *s)
{
    DWORD n;
    WriteConsoleW(out, s, (DWORD)lstrlenW(s), &n, 0);
}

static void at(HANDLE out, int row, int col)
{
    char seq[32];
    snprintf(seq, sizeof(seq), "\x1b[%d;%dH", row, col);
    put(out, seq);
}

static Snapshot *snapshot(HANDLE out)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(out, &info)) return NULL;
    SHORT cols = info.dwSize.X, rows = info.dwSize.Y;
    Snapshot *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            sizeof(Snapshot) + (size_t)cols * rows * sizeof(CHAR_INFO));
    if (!s) return NULL;
    s->cols = cols;
    s->rows = rows;
    s->cursor_x = info.dwCursorPosition.X;
    s->cursor_y = info.dwCursorPosition.Y;
    SMALL_RECT region = { 0, 0, (SHORT)(cols - 1), (SHORT)(rows - 1) };
    if (!ReadConsoleOutputW(out, s->cells, (COORD){ cols, rows }, (COORD){ 0, 0 }, &region)) {
        HeapFree(GetProcessHeap(), 0, s);
        return NULL;
    }
    return s;
}

static size_t snapshot_size(const Snapshot *s)
{
    return sizeof(Snapshot) + (size_t)s->cols * s->rows * sizeof(CHAR_INFO);
}

/* The full-screen program: draws, saves its cells to @path and leaves the
 * alternate screen unless @keep (a pseudo console's host then sees the
 * screen as it was drawn) */
static int draw(const char *path, BOOL keep)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(out, &info)) return 1;
    int w = info.dwSize.X, h = info.dwSize.Y;
    if (w < 48 || h < 20) return 2;
    char seq[64];

    put(out, "\x1b[?1049h\x1b[H\x1b[2J");
    put(out, "\x1b[1;1H\x1b[44;97m\x1b[2K NovaOS pseudo console screen\x1b[0m");
    at(out, 3, 2); putw(out, L"\x250C\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2510");
    for (int row = 4; row <= 8; row++) {
        at(out, row, 2); putw(out, L"\x2502");
        at(out, row, 23); putw(out, L"\x2502");
    }
    at(out, 9, 2); putw(out, L"\x2514\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2500\x2518");
    put(out, "\x1b[4;4H\x1b[1;31mbold\x1b[0m \x1b[7mrev\x1b[27m \x1b[4mund\x1b[24m \x1b[38;5;46m256\x1b[39m");
    at(out, 5, 4); putw(out, L"wide \x65E5\x672C\x8A9E!");
    put(out, "\x1b[6;4HXXXXXXXXXX\x1b[6;8H\x1b[1K");         /* erase to the start of the line */
    put(out, "\x1b[7;4HYYYYYYYYYY\x1b[7;6H\x1b[3X");         /* erase characters */
    put(out, "\x1b[7;4H\x1b[2@\x1b[7;14H\x1b[1P");          /* insert and delete characters */
    put(out, "\x1b[8;4H\x1b[42mgreen\x1b[K\x1b[0m");          /* erase to the end in the pen's colours */

    put(out, "\x1b[11;20r\x1b[11;1H");                       /* a scroll region */
    for (int i = 1; i <= 13; i++) {
        snprintf(seq, sizeof(seq), "region line %d\n", i);
        put(out, seq);
    }
    put(out, "\x1b[r");
    put(out, "\x1b[13;1H\x1b[2L\x1b[16;1H\x1b[1M");         /* insert and delete lines */
    at(out, 19, 1);
    for (int i = 0; i < w + 7; i++) put(out, i % 10 == 9 ? "|" : "w");   /* wraps onto the next line */

    SetConsoleTextAttribute(out, 0x2E);
    SetConsoleCursorPosition(out, (COORD){ (SHORT)(w - 14), 2 });
    put(out, "api text");
    CHAR_INFO cells[6];
    for (int i = 0; i < 6; i++) {
        cells[i].Char.UnicodeChar = (WCHAR)(L'A' + i);
        cells[i].Attributes = (WORD)(0x10 * (i + 1) + 0xF - i);
    }
    SMALL_RECT region = { (SHORT)(w - 14), 4, (SHORT)(w - 12), 5 };
    WriteConsoleOutputW(out, cells, (COORD){ 3, 2 }, (COORD){ 0, 0 }, &region);
    DWORD done;
    FillConsoleOutputCharacterW(out, L'#', 6, (COORD){ (SHORT)(w - 14), 7 }, &done);
    FillConsoleOutputAttribute(out, 0x5F, 6, (COORD){ (SHORT)(w - 14), 7 }, &done);
    CHAR_INFO fill;
    fill.Char.UnicodeChar = L'.';
    fill.Attributes = 0x1B;
    region = (SMALL_RECT){ (SHORT)(w - 14), 4, (SHORT)(w - 12), 5 };
    ScrollConsoleScreenBufferW(out, &region, NULL, (COORD){ (SHORT)(w - 8), 9 }, &fill);
    SetConsoleTextAttribute(out, 7);

    at(out, h, 1);
    put(out, "last line\nafter the screen scrolled");             /* the whole screen scrolls up a line */
    SetConsoleCursorPosition(out, (COORD){ 5, 5 });

    Snapshot *s = snapshot(out);
    if (!keep) put(out, "\x1b[?1049l");
    if (!s) return 3;
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 4;
    DWORD n = 0;
    BOOL ok = WriteFile(f, s, (DWORD)snapshot_size(s), &n, NULL) && n == snapshot_size(s);
    CloseHandle(f);
    return ok ? 0 : 5;
}

static Snapshot *load(const char *path)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(f, NULL), n = 0;
    Snapshot *s = size >= sizeof(Snapshot) ? HeapAlloc(GetProcessHeap(), 0, size) : NULL;
    if (s && (!ReadFile(f, s, size, &n, NULL) || n != size || size != snapshot_size(s))) {
        HeapFree(GetProcessHeap(), 0, s);
        s = NULL;
    }
    CloseHandle(f);
    return s;
}

/* Cells and cursor equal; the first differences printed */
static BOOL same(const char *what, const Snapshot *a, const Snapshot *b)
{
    if (a->cols != b->cols || a->rows != b->rows) {
        printf("%s: %dx%d against %dx%d\n", what, a->cols, a->rows, b->cols, b->rows);
        return FALSE;
    }
    int diffs = 0;
    for (int i = 0; i < a->cols * a->rows; i++) {
        if (a->cells[i].Char.UnicodeChar == b->cells[i].Char.UnicodeChar &&
            a->cells[i].Attributes == b->cells[i].Attributes) continue;
        if (diffs++ < 4)
            printf("%s: cell %d,%d U+%04X/%04X against U+%04X/%04X\n", what, i % a->cols, i / a->cols,
                   a->cells[i].Char.UnicodeChar, a->cells[i].Attributes,
                   b->cells[i].Char.UnicodeChar, b->cells[i].Attributes);
    }
    if (diffs) printf("%s: %d cells differ\n", what, diffs);
    if (a->cursor_x != b->cursor_x || a->cursor_y != b->cursor_y) {
        printf("%s: cursor %d,%d against %d,%d\n", what, a->cursor_x, a->cursor_y, b->cursor_x, b->cursor_y);
        return FALSE;
    }
    return !diffs;
}

typedef struct { HANDLE pipe; char *data; DWORD len, cap; } Output;

static DWORD WINAPI read_output(LPVOID arg)
{
    Output *o = arg;
    char buf[4096];
    DWORD got;
    while (ReadFile(o->pipe, buf, sizeof(buf), &got, NULL) && got) {
        if (o->len + got > o->cap) {
            DWORD cap = (o->cap ? o->cap * 2 : 65536) + got;
            char *data = o->data ? HeapReAlloc(GetProcessHeap(), 0, o->data, cap) : HeapAlloc(GetProcessHeap(), 0, cap);
            if (!data) break;
            o->data = data;
            o->cap = cap;
        }
        memcpy(o->data + o->len, buf, got);
        o->len += got;
    }
    return 0;
}

static DWORD run(char *cmdline, STARTUPINFOEXA *si, DWORD flags)
{
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, flags, NULL, NULL, (LPSTARTUPINFOA)si, &pi)) return 100;
    DWORD code = 101;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

/* The child under a pseudo console of @size; its output in @o */
static DWORD run_pseudo(char *cmdline, COORD size, Output *o)
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE in_read, in_write, out_read, out_write;
    if (!CreatePipe(&in_read, &in_write, &sa, 0)) return 110;
    if (!CreatePipe(&out_read, &out_write, &sa, 0)) return 111;
    HPCON pc = NULL;
    if (CreatePseudoConsole(size, in_read, out_write, 0, &pc) != S_OK) return 112;
    CloseHandle(in_read);
    CloseHandle(out_write);
    o->pipe = out_read;
    HANDLE reader = CreateThread(NULL, 0, read_output, o, 0, NULL);
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = HeapAlloc(GetProcessHeap(), 0, attr_size);
    DWORD code = 113;
    if (attrs && InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size) &&
        UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pc, sizeof(pc), NULL, NULL)) {
        STARTUPINFOEXA si;
        memset(&si, 0, sizeof(si));
        si.StartupInfo.cb = sizeof(si);
        si.lpAttributeList = attrs;
        code = run(cmdline, &si, EXTENDED_STARTUPINFO_PRESENT);
        DeleteProcThreadAttributeList(attrs);
    }
    if (attrs) HeapFree(GetProcessHeap(), 0, attrs);
    CloseHandle(in_write);
    ClosePseudoConsole(pc);
    if (reader) {
        WaitForSingleObject(reader, INFINITE);
        CloseHandle(reader);
    }
    CloseHandle(out_read);
    return code;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "draw")) return draw(argv[2], argc >= 4);

    char self[MAX_PATH], dir[MAX_PATH], window_path[MAX_PATH], pseudo_path[MAX_PATH], cmdline[3 * MAX_PATH];
    GetModuleFileNameA(NULL, self, sizeof(self));
    GetTempPathA(sizeof(dir), dir);
    snprintf(window_path, sizeof(window_path), "%sptyscreen-window.bin", dir);
    snprintf(pseudo_path, sizeof(pseudo_path), "%sptyscreen-pseudo.bin", dir);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(out, &info)) {
        printf("ptyscreentest: not on a console\n");
        return 1;
    }
    COORD size = info.dwSize;

    /* In this console (the Terminal's window) */
    STARTUPINFOEXA si;
    memset(&si, 0, sizeof(si));
    si.StartupInfo.cb = sizeof(si.StartupInfo);
    snprintf(cmdline, sizeof(cmdline), "\"%s\" draw \"%s\"", self, window_path);
    DWORD code = run(cmdline, &si, 0);
    CHECK("draws in the console window", code == 0);
    if (code) printf("window child: exit code %lu\n", code);

    /* Under a pseudo console of the same size */
    Output o = { 0 };
    snprintf(cmdline, sizeof(cmdline), "\"%s\" draw \"%s\" keep", self, pseudo_path);
    code = run_pseudo(cmdline, size, &o);
    CHECK("draws under a pseudo console", code == 0);
    if (code) printf("pseudo console child: exit code %lu\n", code);
    CHECK("the pseudo console's host receives output", o.len > 0);
    printf("pseudo console output: %lu bytes for %dx%d cells\n", o.len, size.X, size.Y);

    Snapshot *window = load(window_path), *pseudo = load(pseudo_path), *host = NULL;
    CHECK("window cells saved", window != NULL);
    CHECK("pseudo console cells saved", pseudo != NULL);

    /* The host's view: the pseudo console's output through this console's
     * emulator, in a screen buffer of the same size */
    HANDLE screen = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, 0, NULL, 1, NULL);
    CHECK("host screen buffer", screen != INVALID_HANDLE_VALUE && SetConsoleScreenBufferSize(screen, size));
    if (screen != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        CHECK("host draws the output", o.len && WriteConsoleA(screen, o.data, o.len, &written, NULL) && written == o.len);
        host = snapshot(screen);
        CloseHandle(screen);
    }
    CHECK("host cells read", host != NULL);
    if (window && pseudo) CHECK("the program reads back the same screen in a window and a pseudo console",
                                same("window/pseudo", window, pseudo));
    if (pseudo && host) CHECK("the pseudo console's host draws the program's cells", same("pseudo/host", pseudo, host));
    if (window && host) CHECK("the host draws what the console window holds", same("window/host", window, host));
    DeleteFileA(window_path);
    DeleteFileA(pseudo_path);
    printf("ptyscreentest: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
