/* dlltest.exe — DllMain, exported calls, static TLS, LoadLibrary,
 * GetModuleFileName with more than 64 modules loaded, exports found by
 * ordinal alone, system DLL calls Delphi programs and LibreOffice
 * make at start, and the system's delay-load helper */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <objbase.h>
#include <commctrl.h>

/* Statically imported from testdll.dll */
__declspec(dllimport) int testdll_add(int a, int b);
__declspec(dllimport) int testdll_get_tls(void);
__declspec(dllimport) void testdll_set_tls(int v);
__declspec(dllimport) long testdll_process_attach(void);
__declspec(dllimport) long testdll_thread_attach(void);

static int pass, fail;
#define CHECK(c) do{ if(c) pass++; else { fail++; printf("FAIL line %d: %s\n",__LINE__,#c);} }while(0)

static volatile int tls_ok;
static DWORD WINAPI tworker(LPVOID a)
{
    (void)a;
    int before = testdll_get_tls();      /* fresh per-thread copy: initializer value */
    testdll_set_tls(0x1234);
    Sleep(2);
    if (before == 0xABCD && testdll_get_tls() == 0x1234) InterlockedIncrement((LONG*)&tls_ok);
    return 0;
}

/* Exports Windows has by ordinal alone: uxtheme's dark mode (WinSCP and
 * Notepad++ call ordinal 135, SetPreferredAppMode, with one argument; an
 * ordinal Windows leaves empty, such as 134, is NULL, not another export) */
static void ordinals(void)
{
    HMODULE ux = LoadLibraryA("uxtheme.dll");
    CHECK(ux != NULL);
    typedef int (WINAPI *ModeFn)(int);
    typedef BOOL (WINAPI *WndFn)(HWND, BOOL);
    ModeFn mode = (ModeFn)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    WndFn allow = (WndFn)GetProcAddress(ux, MAKEINTRESOURCEA(133));
    CHECK(mode && mode(1) == 0 && mode(0) == 1);
    CHECK(allow && !allow(NULL, TRUE));
    CHECK(GetProcAddress(ux, MAKEINTRESOURCEA(104)) != NULL);
    CHECK(GetProcAddress(ux, MAKEINTRESOURCEA(134)) == NULL);
}

/* An image list written to a stream and read back (Delphi keeps its forms'
 * image lists this way): five 16x16 images, four to a row in the stream's
 * bitmap, the fifth drawn from the second row */
static void imagelist_stream(void)
{
    static const COLORREF colors[5] = { RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255), RGB(255, 255, 255), RGB(200, 100, 50) };
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), 80, -16, 1, 32, BI_RGB, 0, 0, 0, 0, 0 } };
    DWORD *px = NULL;
    HBITMAP bm = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    CHECK(bm && px);
    if (!bm || !px) return;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 80; x++) {
            COLORREF c = colors[x / 16];
            px[y * 80 + x] = (DWORD)GetRValue(c) << 16 | (DWORD)GetGValue(c) << 8 | GetBValue(c);
        }
    HIMAGELIST il = ImageList_Create(16, 16, ILC_COLOR24 | ILC_MASK, 5, 2);
    CHECK(il && ImageList_Add(il, bm, NULL) == 0 && ImageList_GetImageCount(il) == 5);
    IStream *s = NULL;
    CHECK(CreateStreamOnHGlobal(NULL, TRUE, &s) == S_OK && s);
    if (!il || !s) return;
    CHECK(ImageList_Write(il, s));
    LARGE_INTEGER zero = { 0 };
    char magic[2] = { 0 };
    ULONG got = 0;
    IStream_Seek(s, zero, STREAM_SEEK_SET, NULL);
    CHECK(s->lpVtbl->Read(s, magic, 2, &got) == S_OK && got == 2 && magic[0] == 'I' && magic[1] == 'L');
    IStream_Seek(s, zero, STREAM_SEEK_SET, NULL);
    HIMAGELIST back = ImageList_Read(s);
    int cx = 0, cy = 0;
    CHECK(back && ImageList_GetImageCount(back) == 5 && ImageList_GetIconSize(back, &cx, &cy) && cx == 16 && cy == 16);
    HDC dc = CreateCompatibleDC(NULL);
    BITMAPINFO tbi = { { sizeof(BITMAPINFOHEADER), 16, -16, 1, 32, BI_RGB, 0, 0, 0, 0, 0 } };
    DWORD *tp = NULL;
    HBITMAP target = CreateDIBSection(NULL, &tbi, DIB_RGB_COLORS, (void **)&tp, NULL, 0);
    HGDIOBJ old = SelectObject(dc, target);
    CHECK(back && ImageList_Draw(back, 4, dc, 0, 0, ILD_NORMAL));
    GdiFlush();
    CHECK(tp && (tp[8 * 16 + 8] & 0xFFFFFF) == 0xC86432);       /* RGB(200, 100, 50) */
    SelectObject(dc, old);
    DeleteObject(target);
    DeleteDC(dc);
    if (back) ImageList_Destroy(back);
    ImageList_Destroy(il);
    IStream_Release(s);
    DeleteObject(bm);
}

/* The shell namespace: the desktop folder parses a path, names it, binds
 * to it and lists it (WinSCP's drive tree starts from SHGetDesktopFolder);
 * called through the vtables, as programs see IShellFolder and IEnumIDList */
typedef HRESULT (WINAPI *DeskFn)(void **);
typedef HRESULT (WINAPI *ParseFn)(void *, HWND, void *, LPWSTR, ULONG *, void **, ULONG *);
typedef HRESULT (WINAPI *EnumFn)(void *, HWND, DWORD, void **);
typedef HRESULT (WINAPI *BindFn)(void *, const void *, void *, REFIID, void **);
typedef HRESULT (WINAPI *NameFn)(void *, const void *, DWORD, void *);
typedef HRESULT (WINAPI *NextFn)(void *, ULONG, void **, ULONG *);
typedef ULONG (WINAPI *RelFn)(void *);
#define VT(o, i) ((*(void ***)(o))[i])
static void shell_folders(void)
{
    static const GUID iid_folder = { 0x000214E6, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
    DeskFn desk = (DeskFn)GetProcAddress(LoadLibraryA("shell32.dll"), "SHGetDesktopFolder");
    void *top = NULL, *pidl = NULL, *win = NULL, *en = NULL;
    CHECK(desk && desk(&top) == S_OK && top);
    if (!top) return;
    ULONG eaten = 0, attrs = 0x20000000;                    /* SFGAO_FOLDER */
    WCHAR name[] = L"C:\\Windows";
    CHECK(((ParseFn)VT(top, 3))(top, NULL, NULL, name, &eaten, &pidl, &attrs) == S_OK && pidl && attrs == 0x20000000);
    struct { UINT type; union { LPWSTR w; char c[MAX_PATH]; }; } sr = { 0 };
    CHECK(pidl && ((NameFn)VT(top, 11))(top, pidl, 1 /* SHGDN_INFOLDER */, &sr) == S_OK && sr.type == 0 && !lstrcmpiW(sr.w, L"Windows"));
    if (sr.type == 0 && sr.w) CoTaskMemFree(sr.w);
    CHECK(pidl && ((BindFn)VT(top, 5))(top, pidl, NULL, &iid_folder, &win) == S_OK && win);
    BOOL system32 = FALSE;
    if (win && ((EnumFn)VT(win, 4))(win, NULL, 0x20 | 0x40, &en) == S_OK && en) {
        void *item;
        ULONG got;
        while (((NextFn)VT(en, 3))(en, 1, &item, &got) == S_OK && got == 1) {
            if (((NameFn)VT(win, 11))(win, item, 1, &sr) == S_OK && sr.type == 0) {
                system32 |= !lstrcmpiW(sr.w, L"System32");
                CoTaskMemFree(sr.w);
            }
            CoTaskMemFree(item);
        }
        ((RelFn)VT(en, 2))(en);
    }
    CHECK(system32);
    if (win) ((RelFn)VT(win, 2))(win);
    CoTaskMemFree(pidl);
    ((RelFn)VT(top, 2))(top);
}

/* A DLL the program imports refuses to load (its DllMain returns FALSE):
 * as on Windows, the process ends with STATUS_DLL_INIT_FAILED before its
 * entry point runs, here a copy of dlltest started with
 * NOVA_TESTDLL_REFUSE set, which testdll's DllMain answers with FALSE */
static void refused_start(void)
{
    char exe[MAX_PATH], cmd[MAX_PATH + 16];
    GetModuleFileNameA(0, exe, sizeof(exe));
    snprintf(cmd, sizeof(cmd), "\"%s\" child", exe);
    SetEnvironmentVariableA("NOVA_TESTDLL_REFUSE", "1");
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessA(exe, cmd, 0, 0, FALSE, 0, 0, 0, &si, &pi);
    SetEnvironmentVariableA("NOVA_TESTDLL_REFUSE", 0);
    CHECK(ok);
    if (!ok) return;
    CHECK(WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    if (code != 0xC0000142) printf("refused DllMain: exit code 0x%08lx\n", (unsigned long)code);
    CHECK(code == 0xC0000142);                   /* STATUS_DLL_INIT_FAILED */
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

/* GetModuleFileName and EnumProcessModules see every module, not just the
 * first 64 (GOG Galaxy's client has 89 at start, and MFC's DllMain fails
 * when GetModuleFileName does not know mfc140u.dll) */
static void many_modules(void)
{
    static const char *const names[] = {
        "activeds", "advapi32", "authz", "bcrypt", "bcryptprimitives", "cabinet", "cfgmgr32",
        "comctl32", "comdlg32", "credui", "crypt32", "dbghelp", "dwmapi", "gdi32", "imagehlp",
        "imm32", "kernelbase", "ktmw32", "mpr", "mscms", "msftedit", "msxml6", "ncrypt",
        "netapi32", "normaliz", "ole32", "oleacc", "oleaut32", "pdh", "powrprof", "propsys",
        "psapi", "riched20", "rpcrt4", "secur32", "sensapi", "setupapi", "shell32", "shfolder",
        "shlwapi", "taskschd", "user32", "userenv", "usp10", "uxtheme", "version", "wevtapi",
        "winspool", "wintab32", "wintrust", "winusb", "wldap32", "wsock32", "wtsapi32",
        "x3daudio1_7", "xinput1_1", "xinput1_2", "xinput1_3", "xinput1_4", "xinput9_1_0",
    };
    char path[MAX_PATH];
    HMODULE last = 0;
    const char *last_name = "";
    for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
        HMODULE m = LoadLibraryA(names[i]);
        if (m) { last = m; last_name = names[i]; }
    }
    typedef BOOL (WINAPI *enumfn)(HANDLE, HMODULE *, DWORD, LPDWORD);
    enumfn en = (enumfn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32EnumProcessModules");
    CHECK(en != NULL);
    if (!en) return;
    static HMODULE mods[1024];
    DWORD need = 0;
    CHECK(en(GetCurrentProcess(), mods, sizeof(mods), &need));
    DWORD n = need / sizeof(HMODULE);
    if (n <= 64) printf("many modules: only %lu loaded\n", (unsigned long)n);
    CHECK(n > 64);
    BOOL found = FALSE;
    int unnamed = 0;
    for (DWORD i = 0; i < n && i < 1024; i++) {
        if (mods[i] == last) found = TRUE;
        if (!GetModuleFileNameA(mods[i], path, sizeof(path))) {
            if (!unnamed++) printf("many modules: no file name for module %lu\n", (unsigned long)i);
        }
    }
    CHECK(found);
    CHECK(unnamed == 0);
    DWORD k = last ? GetModuleFileNameA(last, path, sizeof(path)) : 0;
    const char *base = strrchr(path, '\\');
    CHECK(k > 0 && base && !_strnicmp(base + 1, last_name, strlen(last_name)));
}

/* What LibreOffice imports at start: d2d1's D2D1CreateFactory by ordinal 1
 * (and D2D1MakeRotateMatrix by 2), oledlg.dll and fontsub.dll, the
 * clipboard's format list and the C runtime's _s path functions */
static void libreoffice(void)
{
    HMODULE d2 = LoadLibraryA("d2d1.dll");
    CHECK(d2 != NULL);
    CHECK(d2 && GetProcAddress(d2, MAKEINTRESOURCEA(1)) == GetProcAddress(d2, "D2D1CreateFactory"));
    CHECK(d2 && GetProcAddress(d2, MAKEINTRESOURCEA(2)) == GetProcAddress(d2, "D2D1MakeRotateMatrix"));
    HMODULE ole = LoadLibraryA("oledlg.dll"), fs = LoadLibraryA("fontsub.dll");
    typedef UINT (WINAPI *InsertFn)(void *);
    InsertFn ins = ole ? (InsertFn)GetProcAddress(ole, "OleUIInsertObjectW") : NULL;
    CHECK(ins && ins(NULL) == 2);                        /* OLEUI_CANCEL */
    CHECK(fs && GetProcAddress(fs, "CreateFontPackage") != NULL);
    typedef BOOL (WINAPI *FormatsFn)(UINT *, UINT, UINT *);
    FormatsFn formats = (FormatsFn)GetProcAddress(GetModuleHandleA("user32.dll"), "GetUpdatedClipboardFormats");
    UINT list[64], n = 99;
    CHECK(formats && formats(list, 64, &n) && n <= 64);
    HMODULE crt = LoadLibraryA("ucrtbase.dll");
    typedef int (__cdecl *SplitFn)(const wchar_t *, wchar_t *, size_t, wchar_t *, size_t, wchar_t *, size_t,
                                   wchar_t *, size_t);
    typedef int (__cdecl *MakeFn)(wchar_t *, size_t, const wchar_t *, const wchar_t *, const wchar_t *, const wchar_t *);
    SplitFn split = crt ? (SplitFn)GetProcAddress(crt, "_wsplitpath_s") : NULL;
    MakeFn make = crt ? (MakeFn)GetProcAddress(crt, "_wmakepath_s") : NULL;
    wchar_t drv[4], dir[64], name[16], ext[8], path[64], tiny[4];
    CHECK(split && split(L"C:\\Apps\\data\\letter.docx", drv, 4, dir, 64, name, 16, ext, 8) == 0 &&
          !wcscmp(drv, L"C:") && !wcscmp(dir, L"\\Apps\\data\\") && !wcscmp(name, L"letter") && !wcscmp(ext, L".docx"));
    CHECK(split && split(L"C:\\Apps\\letter.docx", NULL, 0, NULL, 0, tiny, 4, NULL, 0) == 34 && !tiny[0]);
    CHECK(make && make(path, 64, L"C:", L"\\Apps", L"sums", L"xlsx") == 0 && !wcscmp(path, L"C:\\Apps\\sums.xlsx"));
    CHECK(make && make(tiny, 4, L"C:", L"\\Apps", L"sums", L"xlsx") == 34);
}

/* The system's delay-load helper (api-ms-win-core-delayload's
 * ResolveDelayLoadedAPI, which Windows' own apps link against through
 * dloadhelper.lib): a descriptor of this image's (its RVAs from this
 * image's base) binds a slot on first use; a DLL that is missing raises
 * ERROR_DELAY_LOAD_FAILED (1285) for the program's handler, as .NET
 * Native's runtime expects when it probes mrt100.dll */
static struct { DWORD attrs, dll, handle, iat, names, bound, unload, stamp; } dl_desc[2];
static HMODULE dl_handle[2];
static ULONG_PTR dl_iat[2][2], dl_names[2][2];
static char dl_dll[] = "kernel32.dll", dl_missing[] = "nosuchdll.dll";
static struct { WORD hint; char name[16]; } dl_proc = { 0, "GetTickCount" };

static void delay_loads(void)
{
    typedef PVOID (WINAPI *ResolveFn)(PVOID, const void *, PVOID, PVOID, ULONG_PTR *, ULONG);
    ResolveFn resolve = (ResolveFn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "ResolveDelayLoadedAPI");
    CHECK(resolve != NULL);
    if (!resolve) return;
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    const char *names[2] = { dl_dll, dl_missing };
    for (int i = 0; i < 2; i++) {
        dl_desc[i].attrs = 1;                                   /* RVA-based */
        dl_desc[i].dll = (DWORD)((BYTE *)names[i] - base);
        dl_desc[i].handle = (DWORD)((BYTE *)&dl_handle[i] - base);
        dl_desc[i].iat = (DWORD)((BYTE *)dl_iat[i] - base);
        dl_desc[i].names = (DWORD)((BYTE *)dl_names[i] - base);
        dl_names[i][0] = (ULONG_PTR)((BYTE *)&dl_proc - base);
    }
    PVOID f = resolve(base, &dl_desc[0], NULL, NULL, &dl_iat[0][0], 0);
    CHECK(f != NULL && f == (PVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetTickCount"));
    CHECK(dl_iat[0][0] == (ULONG_PTR)f && dl_handle[0] == GetModuleHandleA("kernel32.dll"));
    DWORD code = 0;
    __try {
        resolve(base, &dl_desc[1], NULL, NULL, &dl_iat[1][0], 0);
    } __except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
    }
    CHECK(code == 1285 && dl_iat[1][0] == 0);                  /* ERROR_DELAY_LOAD_FAILED */
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "child")) {
        printf("FAIL: the child started although testdll refused to load\n");   /* (never runs) */
        return 3;
    }

    /* Static import works and DllMain(PROCESS_ATTACH) ran once at load */
    CHECK(testdll_add(20, 22) == 42);
    CHECK(testdll_process_attach() == 1);

    /* This (first) thread saw its TLS initializer */
    CHECK(testdll_get_tls() == 0xABCD);
    testdll_set_tls(0x9999);

    /* New threads: DLL_THREAD_ATTACH fires and each gets its own TLS */
    long attach_before = testdll_thread_attach();
    HANDLE t[3];
    for (int i = 0; i < 3; i++) t[i] = CreateThread(0, 0, tworker, 0, 0, 0);
    WaitForMultipleObjects(3, t, TRUE, 5000);
    for (int i = 0; i < 3; i++) CloseHandle(t[i]);
    CHECK(testdll_thread_attach() == attach_before + 3);
    CHECK(tls_ok == 3);
    CHECK(testdll_get_tls() == 0x9999);          /* our TLS untouched by the workers */

    /* GetProcAddress on the already-loaded module */
    HMODULE h = GetModuleHandleA("testdll.dll");
    CHECK(h != NULL);
    typedef int (*addfn)(int, int);
    addfn f = (addfn)GetProcAddress(h, "testdll_add");
    CHECK(f != NULL && f(100, 1) == 101);

    /* LoadLibrary of a not-yet-loaded DLL, then GetProcAddress */
    HMODULE k = LoadLibraryA("kernel32.dll");
    CHECK(k != NULL);
    void *p = (void *)GetProcAddress(k, "Sleep");
    CHECK(p != NULL);
    FreeLibrary(k);

    many_modules();
    ordinals();
    imagelist_stream();
    shell_folders();
    refused_start();
    libreoffice();
    delay_loads();

    printf("DLL/TLS self-test: %d passed, %d failed\n", pass, fail);
    return fail;
}
