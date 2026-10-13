/* A terminal host running an interactive cmd.exe through a pseudo
 * console, as Windows Terminal does: it types keys as a terminal sends
 * them (characters, Backspace as DEL, Ctrl+C, VT sequences for arrows,
 * Delete and F1), resizes the pseudo console while cmd.exe runs, draws the
 * output in a screen buffer of its own at the terminal's size and checks
 * that it shows what the pseudo console holds.  Then "exit" ends cmd.exe
 * and the output pipe ends with the pseudo console. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int passed, failed;
#define CHECK(name, ok) do { if (ok) passed++; else { failed++; printf("FAIL: %s\n", name); } } while (0)

#define WAIT_MS 120000                  /* (cmd.exe under TCG) */
#define EVENT_MODIFY_STATE 0x0002
#define KEY_EVENT 0x0001
#define LEFT_CTRL_PRESSED 0x0008

/* Console input records (the headers here are thin) */
typedef struct {
    WORD EventType;
    union {
        struct { BOOL bKeyDown; WORD wRepeatCount, wVirtualKeyCode, wVirtualScanCode;
                 union { WCHAR UnicodeChar; CHAR AsciiChar; } uChar; DWORD dwControlKeyState; } KeyEvent;
        BYTE pad[16];
    } Event;
} INPUT_RECORD;
WINBASEAPI BOOL WINAPI ReadConsoleInputW(HANDLE h, INPUT_RECORD *rec, DWORD n, LPDWORD read);

typedef struct {
    SHORT cols, rows, cursor_x, cursor_y;
    CHAR_INFO cells[1];
} Snapshot;

static size_t snapshot_size(const Snapshot *s)
{
    return sizeof(Snapshot) + (size_t)s->cols * s->rows * sizeof(CHAR_INFO);
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

/* Characters and cursor equal (the host draws in its own colours); the
 * first differences printed */
static BOOL same(const Snapshot *a, const Snapshot *b)
{
    if (a->cols != b->cols || a->rows != b->rows) {
        printf("pseudo/host: %dx%d against %dx%d\n", a->cols, a->rows, b->cols, b->rows);
        return FALSE;
    }
    int diffs = 0;
    for (int i = 0; i < a->cols * a->rows; i++) {
        if (a->cells[i].Char.UnicodeChar == b->cells[i].Char.UnicodeChar) continue;
        if (diffs++ < 4)
            printf("pseudo/host: cell %d,%d U+%04X against U+%04X\n", i % a->cols, i / a->cols,
                   a->cells[i].Char.UnicodeChar, b->cells[i].Char.UnicodeChar);
    }
    if (diffs) printf("pseudo/host: %d cells differ\n", diffs);
    if (a->cursor_x != b->cursor_x || a->cursor_y != b->cursor_y) {
        printf("pseudo/host: cursor %d,%d against %d,%d\n", a->cursor_x, a->cursor_y, b->cursor_x, b->cursor_y);
        return FALSE;
    }
    return !diffs;
}

/* A row of a snapshot as text, trailing spaces dropped */
static void row_text(const Snapshot *s, int y, char *out, int cap)
{
    int n = 0;
    for (int x = 0; x < s->cols && n < cap - 1; x++) {
        WCHAR ch = s->cells[y * s->cols + x].Char.UnicodeChar;
        out[n++] = ch >= ' ' && ch < 0x7F ? (char)ch : ch ? '?' : ' ';
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
}

static BOOL has_row(const Snapshot *s, const char *text)
{
    char row[512];
    for (int y = 0; y < s->rows; y++) {
        row_text(s, y, row, sizeof(row));
        if (strstr(row, text)) return TRUE;
    }
    return FALSE;
}

/* ---- children, started from cmd.exe ---- */

/* "keys": the key presses of a raw read, as VK codes (c: with Ctrl), up to q */
static int keys_child(void)
{
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0, n;
    if (!GetConsoleMode(in, &mode) || !SetConsoleMode(in, 0)) return 1;
    char line[256] = "keys:";
    WriteConsoleA(out, "ready\n", 6, &n, 0);
    for (;;) {
        INPUT_RECORD r;
        if (!ReadConsoleInputW(in, &r, 1, &n) || n != 1) break;
        if (r.EventType != KEY_EVENT || !r.Event.KeyEvent.bKeyDown) continue;
        char item[16];
        snprintf(item, sizeof(item), " %02X%s", r.Event.KeyEvent.wVirtualKeyCode,
                 r.Event.KeyEvent.dwControlKeyState & LEFT_CTRL_PRESSED ? "c" : "");
        if (strlen(line) + strlen(item) < sizeof(line)) strcat(line, item);
        if (r.Event.KeyEvent.uChar.UnicodeChar == 'q') break;
    }
    SetConsoleMode(in, mode);
    strcat(line, "\n");
    WriteConsoleA(out, line, (DWORD)strlen(line), &n, 0);
    return 0;
}

/* "snap PATH": the console's size printed, its cells saved to PATH, then
 * a wait (the host reads what it has drawn meanwhile) */
static int snap_child(const char *path)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(out, &info)) return 1;
    char line[64];
    DWORD n;
    snprintf(line, sizeof(line), "size %dx%d\n", info.dwSize.X, info.dwSize.Y);
    WriteConsoleA(out, line, (DWORD)strlen(line), &n, 0);
    Snapshot *s = snapshot(out);
    if (!s) return 2;
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 3;
    BOOL ok = WriteFile(f, s, (DWORD)snapshot_size(s), &n, NULL) && n == snapshot_size(s);
    CloseHandle(f);
    HANDLE snapped = OpenEventA(EVENT_MODIFY_STATE, FALSE, "conptytest-snapped");
    HANDLE go = OpenEventA(SYNCHRONIZE, FALSE, "conptytest-go");
    if (!ok || !snapped || !go) return 4;
    SetEvent(snapped);
    WaitForSingleObject(go, WAIT_MS);
    return 0;
}

/* ---- the terminal host ---- */

typedef struct {
    HANDLE pipe, more;
    CRITICAL_SECTION lock;
    char *data;
    DWORD len, cap;
    volatile LONG ended;
} Output;

static DWORD WINAPI read_output(LPVOID arg)
{
    Output *o = arg;
    char buf[4096];
    DWORD got;
    while (ReadFile(o->pipe, buf, sizeof(buf), &got, NULL) && got) {
        EnterCriticalSection(&o->lock);
        if (o->len + got + 1 > o->cap) {
            DWORD cap = (o->cap ? o->cap * 2 : 65536) + got + 1;
            char *data = o->data ? HeapReAlloc(GetProcessHeap(), 0, o->data, cap) : HeapAlloc(GetProcessHeap(), 0, cap);
            if (!data) { LeaveCriticalSection(&o->lock); break; }
            o->data = data;
            o->cap = cap;
        }
        memcpy(o->data + o->len, buf, got);
        o->len += got;
        o->data[o->len] = 0;
        LeaveCriticalSection(&o->lock);
        SetEvent(o->more);
    }
    InterlockedExchange(&o->ended, 1);
    SetEvent(o->more);
    return 0;
}

/* Wait until the output from @from on holds @text; its end in *@at */
static BOOL wait_for(Output *o, DWORD from, const char *text, DWORD *at)
{
    DWORD start = GetTickCount();
    for (;;) {
        EnterCriticalSection(&o->lock);
        const char *hit = o->len > from ? strstr(o->data + from, text) : NULL;
        if (hit && at) *at = (DWORD)(hit - o->data) + (DWORD)strlen(text);
        LeaveCriticalSection(&o->lock);
        if (hit) return TRUE;
        if (o->ended || GetTickCount() - start > WAIT_MS) {
            printf("waited for \"%s\"\n", text);
            return FALSE;
        }
        WaitForSingleObject(o->more, 1000);
    }
}

/* Wait until no output has come for a while; its length */
static DWORD quiet(Output *o)
{
    DWORD len = (DWORD)-1;
    for (int i = 0; i < 100; i++) {
        EnterCriticalSection(&o->lock);
        DWORD now = o->len;
        LeaveCriticalSection(&o->lock);
        if (now == len) break;
        len = now;
        Sleep(500);
    }
    return len;
}

static DWORD output_len(Output *o)
{
    EnterCriticalSection(&o->lock);
    DWORD len = o->len;
    LeaveCriticalSection(&o->lock);
    return len;
}

/* A terminal's typing: one write per key */
static BOOL type(HANDLE in, const char *keys)
{
    for (const char *k = keys; *k;) {
        int n = 1;
        if (*k == 0x1B) {                               /* a VT sequence goes in one write */
            while (k[n] && k[n] != 0x1B && !(n >= 3 && ((k[n - 1] >= 'A' && k[n - 1] <= 'Z') || k[n - 1] == '~'))) n++;
        }
        DWORD put = 0;
        if (!WriteFile(in, k, (DWORD)n, &put, NULL) || put != (DWORD)n) return FALSE;
        k += n;
        Sleep(20);
    }
    return TRUE;
}

/* The host's screen: the output from @from to @to drawn into @screen */
static BOOL draw(HANDLE screen, Output *o, DWORD from, DWORD to)
{
    if (to <= from) return TRUE;
    DWORD written = 0;
    EnterCriticalSection(&o->lock);
    BOOL ok = WriteConsoleA(screen, o->data + from, to - from, &written, NULL) && written == to - from;
    LeaveCriticalSection(&o->lock);
    return ok;
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

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "keys")) return keys_child();
    if (argc >= 3 && !strcmp(argv[1], "snap")) return snap_child(argv[2]);

    char self[MAX_PATH], cmd[MAX_PATH], snap_path[MAX_PATH], line[3 * MAX_PATH];
    GetModuleFileNameA(NULL, self, sizeof(self));
    GetSystemDirectoryA(cmd, sizeof(cmd));
    strcat(cmd, "\\cmd.exe");
    GetTempPathA(sizeof(snap_path), snap_path);
    strcat(snap_path, "conptytest-snap.bin");

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE in_read, in_write, out_read, out_write;
    if (!CreatePipe(&in_read, &in_write, &sa, 0) || !CreatePipe(&out_read, &out_write, &sa, 0)) {
        printf("conptytest: no pipes\n");
        return 1;
    }
    COORD size = { 80, 25 };
    HPCON pc = NULL;
    HRESULT hr = CreatePseudoConsole(size, in_read, out_write, 0, &pc);
    CHECK("CreatePseudoConsole", hr == S_OK && pc);
    if (hr != S_OK) return 1;
    CloseHandle(in_read);
    CloseHandle(out_write);

    Output o;
    memset(&o, 0, sizeof(o));
    o.pipe = out_read;
    o.more = CreateEventA(NULL, FALSE, FALSE, NULL);
    InitializeCriticalSection(&o.lock);
    HANDLE reader = CreateThread(NULL, 0, read_output, &o, 0, NULL);

    /* The host's screen at the terminal's size */
    HANDLE screen = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, 0, NULL, 1, NULL);
    CHECK("host screen buffer", screen != INVALID_HANDLE_VALUE && SetConsoleScreenBufferSize(screen, size));

    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = HeapAlloc(GetProcessHeap(), 0, attr_size);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    BOOL started = FALSE;
    if (attrs && InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size) &&
        UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pc, sizeof(pc), NULL, NULL)) {
        STARTUPINFOEXA si;
        memset(&si, 0, sizeof(si));
        si.StartupInfo.cb = sizeof(si);
        si.lpAttributeList = attrs;
        snprintf(line, sizeof(line), "\"%s\"", cmd);
        started = CreateProcessA(NULL, line, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                                 (LPSTARTUPINFOA)&si, &pi);
    }
    CHECK("cmd.exe starts under the pseudo console", started);
    if (!started) printf("CreateProcess %s: error %lu\n", cmd, GetLastError());

    DWORD at = 0, mark = 0;
    BOOL prompt = started && wait_for(&o, 0, ">", &at);
    CHECK("cmd.exe shows its prompt", prompt);

    /* A typed command: echoed as typed, a mistake taken back with Backspace (DEL) */
    if (prompt) {
        mark = at;
        CHECK("type a command", type(in_write, "echo hellx\x7Fo pty\r"));
        CHECK("the typed line is echoed, Backspace edits it", wait_for(&o, mark, "echo hellx\b \bo pty\r\n", &at));
        CHECK("cmd.exe runs it", wait_for(&o, at, "hello pty\r\n", &at));
        CHECK("and prompts again", wait_for(&o, at, ">", &at));

        /* Ctrl+C drops the line being typed */
        mark = at;
        CHECK("type and interrupt a line", type(in_write, "dir nothing\x03"));
        CHECK("Ctrl+C drops the line", wait_for(&o, mark, "dir nothing^C\r\n", &at));
        CHECK("and cmd.exe prompts again", wait_for(&o, at, ">", &at));

        /* Keys as VT sequences reach a program reading raw input */
        mark = at;
        snprintf(line, sizeof(line), "\"%s\" keys\r", self);
        CHECK("start a raw-input program", type(in_write, line));
        CHECK("it reads raw input", wait_for(&o, mark, "ready\r\n", &at));
        type(in_write, "\x1b[A\x1b[1;5D\x1b[3~\x1bOP\x1b[15~q");
        CHECK("arrows, Ctrl+Left, Delete, F1 and F5 arrive as keys",
              wait_for(&o, at, "keys: 26 25c 2E 70 74 51\r\n", &at));
        CHECK("and cmd.exe prompts again", wait_for(&o, at, ">", &at));
    }

    /* Resized while cmd.exe runs: the console takes the new size and draws
     * its screen again for the host, which draws at the new size */
    DWORD resized = quiet(&o);
    if (screen != INVALID_HANDLE_VALUE) CHECK("host draws at 80x25", draw(screen, &o, 0, resized));
    size.X = 100;
    size.Y = 30;
    CHECK("ResizePseudoConsole", ResizePseudoConsole(pc, size) == S_OK);
    if (screen != INVALID_HANDLE_VALUE) SetConsoleScreenBufferSize(screen, size);
    CHECK("the screen is drawn again after the resize", wait_for(&o, resized, "\x1b[2J", NULL));

    Snapshot *pseudo = NULL, *host = NULL;
    if (prompt) {
        HANDLE snapped = CreateEventA(NULL, TRUE, FALSE, "conptytest-snapped");
        HANDLE go = CreateEventA(NULL, TRUE, FALSE, "conptytest-go");
        mark = quiet(&o);
        snprintf(line, sizeof(line), "\"%s\" snap \"%s\"\r", self, snap_path);
        CHECK("start a program after the resize", type(in_write, line));
        CHECK("it sees the new size", wait_for(&o, mark, "size 100x30\r\n", &at));
        BOOL ok = WaitForSingleObject(snapped, WAIT_MS) == WAIT_OBJECT_0;
        CHECK("it saves the pseudo console's cells", ok);
        DWORD len = quiet(&o);
        if (screen != INVALID_HANDLE_VALUE) {
            CHECK("host draws at 100x30", draw(screen, &o, resized, len));
            host = snapshot(screen);
        }
        SetEvent(go);
        pseudo = ok ? load(snap_path) : NULL;
        CHECK("pseudo console cells read", pseudo != NULL);
        CHECK("host cells read", host != NULL);
        if (pseudo && host) {
            CHECK("the host shows what the pseudo console holds", same(pseudo, host));
            CHECK("the screen holds the session", has_row(host, ">echo hello pty") && has_row(host, "size 100x30"));
        }
        CHECK("and cmd.exe prompts again", wait_for(&o, at, ">", &at));
        CloseHandle(snapped);
        CloseHandle(go);

        mark = at;
        CHECK("type exit", type(in_write, "exit\r"));
    }

    DWORD code = 99;
    if (started) {
        CHECK("cmd.exe ends on exit", WaitForSingleObject(pi.hProcess, WAIT_MS) == WAIT_OBJECT_0);
        GetExitCodeProcess(pi.hProcess, &code);
        CHECK("with exit code 0", code == 0);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    CloseHandle(in_write);
    ClosePseudoConsole(pc);
    CHECK("the output pipe ends with the pseudo console", reader && WaitForSingleObject(reader, WAIT_MS) == WAIT_OBJECT_0);
    printf("pseudo console output: %lu bytes\n", output_len(&o));
    if (attrs) { DeleteProcThreadAttributeList(attrs); HeapFree(GetProcessHeap(), 0, attrs); }
    if (screen != INVALID_HANDLE_VALUE) CloseHandle(screen);
    CloseHandle(out_read);
    DeleteFileA(snap_path);
    printf("conptytest: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
