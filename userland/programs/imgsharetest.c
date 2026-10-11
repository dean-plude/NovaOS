/* imgsharetest.exe — a program's pages shared with another process that
 * loads the same file at the same address (the kernel's image cache, as
 * each of Firefox's processes loads the same 164 MB xul.dll).
 *
 * `imgsharetest` changes its own writable data, makes a read-only page
 * writable and changes it, loads a DLL at run time, then starts a copy of
 * itself (`imgsharetest child`) while it still runs.  The copy's pages come
 * from the cache: it must see its data, its read-only page and its zeroed
 * data as the file has them, not as the first process changed them, and
 * its imports must work (it calls through them).  The 64-bit build also
 * commits fifteen sixteenths of the RAM at once (past the old cap of 7/8,
 * which shared module pages used to count towards) and must be refused
 * more than the RAM.  It reports the number of failed checks as its exit
 * code. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int pass, fail;
#define CHECK(what, cond) do { if (cond) pass++; else { fail++; printf("FAIL: %s (line %d)\n", what, __LINE__); } } while (0)

static volatile int counter = 7;                    /* .data */
static volatile char zeros[3 * 4096];               /* .bss: zeros, committed on first touch */
static const char marker[2 * 4096] = "imgsharetest read-only marker";   /* .rdata */
#define MARK0 (*(volatile const char *)marker)      /* (read from memory, not the initializer) */

static int child(void)
{
    CHECK("the data as the file has it", counter == 7);
    int nonzero = 0;
    for (int i = 0; i < (int)sizeof(zeros); i++) nonzero += zeros[i] != 0;
    CHECK("zeroed data", nonzero == 0);
    CHECK("the read-only page as the file has it", MARK0 == 'i' && !strcmp((const char *)marker, "imgsharetest read-only marker"));
    /* imports bound in this process: calls through them work */
    char dir[MAX_PATH];
    CHECK("GetSystemDirectoryA", GetSystemDirectoryA(dir, MAX_PATH) > 0);
    HMODULE adv = LoadLibraryA("advapi32.dll");
    CHECK("LoadLibrary advapi32 in the copy", adv != NULL);
    CHECK("GetProcAddress RegOpenKeyExA", adv && GetProcAddress(adv, "RegOpenKeyExA"));
    HKEY k;
    CHECK("RegOpenKeyExA through the DLL", adv &&
          ((LONG (WINAPI *)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY))GetProcAddress(adv, "RegOpenKeyExA"))
              (HKEY_LOCAL_MACHINE, "SOFTWARE", 0, KEY_READ, &k) == ERROR_SUCCESS);
    counter = 8;                                    /* (written: the page is the copy's own) */
    CHECK("the copy writes its data", counter == 8);
    printf("imgsharetest child: %d passed, %d failed\n", pass, fail);
    return fail;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "child")) return child();

    /* change this process's pages: none of it may reach the copy */
    counter = 99;
    zeros[5000] = 1;
    DWORD old;
    char *ro = (char *)marker;
    CHECK("VirtualProtect a read-only page writable", VirtualProtect(ro, 4096, PAGE_READWRITE, &old));
    *(volatile char *)ro = 'X';
    CHECK("the changed read-only page", MARK0 == 'X');
    CHECK("LoadLibrary advapi32", LoadLibraryA("advapi32.dll") != NULL);

    if (sizeof(void *) == 8) {                      /* (a 32-bit process has 2 GB of addresses) */
        MEMORYSTATUSEX ms = { sizeof(ms) };
        GlobalMemoryStatusEx(&ms);
        SIZE_T big = (SIZE_T)(ms.ullTotalPhys / 16 * 15) & ~(SIZE_T)0xFFFF;
        volatile char *m = VirtualAlloc(NULL, big, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        CHECK("commit 15/16 of the RAM", m != NULL);
        if (m) {
            m[0] = 1;
            m[big - 1] = 2;
            CHECK("its first and last pages", m[0] == 1 && m[big - 1] == 2 && m[big / 2] == 0);
            VirtualFree((void *)m, 0, MEM_RELEASE);
        }
        void *over = VirtualAlloc(NULL, (SIZE_T)ms.ullTotalPhys + (64 << 20), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        CHECK("more than the RAM refused", over == NULL);
        if (over) VirtualFree(over, 0, MEM_RELEASE);
    }

    char self[MAX_PATH], cmd[MAX_PATH + 16];
    GetModuleFileNameA(NULL, self, MAX_PATH);
    snprintf(cmd, sizeof(cmd), "\"%s\" child", self);
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    fflush(stdout);
    BOOL started = CreateProcessA(self, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    CHECK("start the copy", started);
    if (started) {
        WaitForSingleObject(pi.hProcess, 60000);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CHECK("the copy's checks", code == 0);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    CHECK("this process's data unchanged by the copy", counter == 99 && MARK0 == 'X');
    printf("imgsharetest: %d passed, %d failed\n", pass, fail);
    return fail;
}
