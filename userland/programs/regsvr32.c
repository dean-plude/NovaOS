/* regsvr32.exe [/u] [/s] DLL — calls a COM server's DllRegisterServer (or DllUnregisterServer).
 * Windows has two, C:\Windows\System32's (64-bit) and SysWOW64's (32-bit),
 * and each hands a DLL of the other's kind to the other one with the same
 * arguments (Inno Setup runs SysWOW64's for every DLL it registers) */
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (__stdcall *REGFN)(void);

#ifdef _WIN64
#define OWN_MACHINE 0x8664
#define OTHER "C:\\Windows\\SysWOW64\\regsvr32.exe"
#else
#define OWN_MACHINE 0x014C
#define OTHER "C:\\Windows\\Sysnative\\regsvr32.exe"
#endif

/* The machine @dll's PE header names (0 if it cannot be read) */
static WORD dll_machine(const char *dll)
{
    char path[MAX_PATH];
    if (!SearchPathA(NULL, dll, ".dll", sizeof(path), path, NULL)) return 0;
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    IMAGE_DOS_HEADER dos;
    DWORD got = 0, pe = 0;
    WORD machine = 0;
    if (!(ReadFile(f, &dos, sizeof(dos), &got, NULL) && got == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
          SetFilePointer(f, dos.e_lfanew, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
          ReadFile(f, &pe, 4, &got, NULL) && got == 4 && pe == IMAGE_NT_SIGNATURE &&
          ReadFile(f, &machine, 2, &got, NULL) && got == 2))
        machine = 0;
    CloseHandle(f);
    return machine;
}

/* Run the other regsvr32 on this command line's arguments; its exit code */
static int hand_over(void)
{
    const char *args = GetCommandLineA();
    if (*args == '"') { args = strchr(args + 1, '"'); args = args ? args + 1 : ""; }
    else while (*args && *args != ' ' && *args != '\t') args++;
    char cmd[2 * MAX_PATH + 64];
    snprintf(cmd, sizeof(cmd), "\"%s\"%s", OTHER, args);
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(OTHER, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "Could not start %s (error %lu).\n", OTHER, GetLastError());
        return 3;
    }
    DWORD code = 3;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

int main(int argc, char **argv)
{
    int unreg = 0, silent = 0;
    const char *dll = 0;
    for (int i = 1; i < argc; i++) {
        if (!_stricmp(argv[i], "/u") || !_stricmp(argv[i], "-u")) unreg = 1;
        else if (!_stricmp(argv[i], "/s") || !_stricmp(argv[i], "-s")) silent = 1;
        else dll = argv[i];
    }
    if (!dll) { fprintf(stderr, "usage: regsvr32 [/u] [/s] DLL\n"); return 1; }
    WORD machine = dll_machine(dll);
    if (machine && machine != OWN_MACHINE && (machine == 0x8664 || machine == 0x014C))
        return hand_over();
    const char *fn = unreg ? "DllUnregisterServer" : "DllRegisterServer";
    HMODULE m = LoadLibraryA(dll);
    if (!m) { fprintf(stderr, "The module \"%s\" failed to load (error %lu).\n", dll, GetLastError()); return 3; }
    REGFN f = (REGFN)GetProcAddress(m, fn);
    if (!f) { fprintf(stderr, "The module \"%s\" has no entry point %s.\n", dll, fn); return 4; }
    HRESULT hr = f();
    if (FAILED(hr)) { fprintf(stderr, "%s in \"%s\" failed: 0x%08lX\n", fn, dll, (unsigned long)hr); return 5; }
    if (!silent) printf("%s in %s succeeded.\n", fn, dll);
    return 0;
}
