/* The DOS host end to end (roadmap step 31.2): ntvdm.exe boots MS-DOS
 * 4.00, built from Microsoft's source, in this console window.  The test
 * waits for COMMAND.COM's prompt on the screen, types VER and DIR /W as a
 * person would (key records into the console's input), reads the answers
 * off the screen ("MS-DOS Version 4.00", COMMAND.COM in the listing), then
 * presses F12 and ntvdm.exe must end and give the screen back. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int passed, failed;
#define CHECK(name, ok) do { if (ok) passed++; else { failed++; printf("FAIL: %s\n", name); } } while (0)

#define WAIT_MS 180000                  /* (a CPU interpreted under TCG) */
#define KEY_EVENT 0x0001

typedef struct {
    WORD EventType;
    union {
        struct { BOOL bKeyDown; WORD wRepeatCount, wVirtualKeyCode, wVirtualScanCode;
                 union { WCHAR UnicodeChar; CHAR AsciiChar; } uChar; DWORD dwControlKeyState; } KeyEvent;
        BYTE pad[16];
    } Event;
} INPUT_RECORD;
WINBASEAPI BOOL WINAPI WriteConsoleInputW(HANDLE h, const INPUT_RECORD *rec, DWORD n, LPDWORD written);

static HANDLE out, in;
static WCHAR screen[200 * 100 + 1];

/* the console's screen as text, rows one after another */
static const WCHAR *read_screen(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    DWORD n = 0;
    screen[0] = 0;
    if (!GetConsoleScreenBufferInfo(out, &info)) return screen;
    DWORD cells = (DWORD)info.dwSize.X * info.dwSize.Y;
    if (cells > 200 * 100) cells = 200 * 100;
    ReadConsoleOutputCharacterW(out, screen, cells, (COORD){ 0, 0 }, &n);
    screen[n] = 0;
    return screen;
}

static int count(const WCHAR *s, const WCHAR *what)
{
    int k = 0;
    for (const WCHAR *p = s; (p = wcsstr(p, what)); p++) k++;
    return k;
}

/* waits until the screen shows @what @times times */
static int wait_for(const WCHAR *what, int times)
{
    for (int ms = 0; ms < WAIT_MS; ms += 250) {
        if (count(read_screen(), what) >= times) return 1;
        Sleep(250);
    }
    printf("dostest: no \"%ls\" (x%d) on the screen; it shows:\n", what, times);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(out, &info))
        for (int y = 0; y < 25 && y < info.dwSize.Y; y++) {
            int x0 = y * info.dwSize.X, len = 80 < info.dwSize.X ? 80 : info.dwSize.X;
            int nonblank = 0;
            for (int x = 0; x < len; x++) if (screen[x0 + x] && screen[x0 + x] != L' ') nonblank = 1;
            if (nonblank) printf("  |%.*ls\n", len, screen + x0);
        }
    return 0;
}

static void press(WORD vk, WCHAR ch)
{
    INPUT_RECORD r[2];
    memset(r, 0, sizeof(r));
    for (int i = 0; i < 2; i++) {
        r[i].EventType = KEY_EVENT;
        r[i].Event.KeyEvent.bKeyDown = i == 0;
        r[i].Event.KeyEvent.wRepeatCount = 1;
        r[i].Event.KeyEvent.wVirtualKeyCode = vk;
        r[i].Event.KeyEvent.uChar.UnicodeChar = ch;
    }
    DWORD n;
    WriteConsoleInputW(in, r, 2, &n);
}

static void type(const char *s)
{
    for (; *s; s++) {
        char c = *s;
        WORD vk = c == '\r' ? VK_RETURN : (c >= 'a' && c <= 'z') ? (WORD)(c - 32) : (WORD)c;
        press(vk, (WCHAR)c);
        Sleep(30);
    }
}

int main(void)
{
    out = GetStdHandle(STD_OUTPUT_HANDLE);
    in = GetStdHandle(STD_INPUT_HANDLE);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    WCHAR cmd[] = L"ntvdm.exe";
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("FAIL: CreateProcess(ntvdm.exe): error %lu\n", GetLastError());
        printf("dostest: 0 passed, 1 failed\n");
        return 1;
    }
    DWORD t0 = GetTickCount();
    int booted = wait_for(L"A:\\>", 1);
    CHECK("MS-DOS boots to the COMMAND.COM prompt", booted);
    DWORD boot_ms = GetTickCount() - t0;        /* (printed at the end: DOS has the screen now) */

    type("VER\r");
    CHECK("VER says MS-DOS Version 4.00", wait_for(L"MS-DOS Version 4.00", 1));
    CHECK("the prompt comes back after VER", wait_for(L"A:\\>", 2));

    type("CLS\r");                              /* (DIR /W then fits on the screen) */
    for (int ms = 0; ms < WAIT_MS && count(read_screen(), L"MS-DOS Version") > 0; ms += 250) Sleep(250);
    type("DIR /W\r");
    CHECK("DIR lists COMMAND.COM", wait_for(L"COMMAND  COM", 1));
    CHECK("DIR lists the rest of MS-DOS (MEM.EXE)", wait_for(L"MEM      EXE", 1));
    CHECK("DIR ends with the free space", wait_for(L"bytes free", 1));
    CHECK("the prompt comes back after DIR", wait_for(L"A:\\>", 2));

    press(VK_F12, 0);
    DWORD w = WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CHECK("F12 ends ntvdm.exe", w == WAIT_OBJECT_0 && code == 0);
    if (w != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
    CHECK("the Terminal's screen comes back", count(read_screen(), L"COMMAND  COM") == 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (booted) printf("dostest: COMMAND.COM prompt after %lu ms\n", boot_ms);
    printf("dostest: %d passed, %d failed\n", passed, failed);
    return failed != 0;
}
