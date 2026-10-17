/* tlssup.c — implicit TLS support (what the MSVC CRT's tlssup.c provides):
 * the .tls section markers, the TLS callback array, _tls_index and the
 * IMAGE_TLS_DIRECTORY (_tls_used) the linker turns into the TLS data
 * directory, and __tlregdtor, which runs a C++ thread_local's destructor
 * when its thread ends (the CRT's tlregdtr.cpp).  Linked into every module
 * that may use __declspec(thread). */
#include <windows.h>
#include <winnt.h>

unsigned long _tls_index = 0;

#pragma section(".tls", long, read, write)
#pragma section(".tls$ZZZ", long, read, write)
__declspec(allocate(".tls")) char _tls_start = 0;
__declspec(allocate(".tls$ZZZ")) char _tls_end = 0;

#pragma section(".CRT$XLA", long, read)
#pragma section(".CRT$XLZ", long, read)
__declspec(allocate(".CRT$XLA")) PIMAGE_TLS_CALLBACK __xl_a = 0;
__declspec(allocate(".CRT$XLZ")) PIMAGE_TLS_CALLBACK __xl_z = 0;

#pragma section(".rdata$T", long, read)
__declspec(allocate(".rdata$T")) const IMAGE_TLS_DIRECTORY _tls_used = {
    (ULONG_PTR)&_tls_start,
    (ULONG_PTR)&_tls_end,
    (ULONG_PTR)&_tls_index,
    (ULONG_PTR)(&__xl_a + 1),
    0, 0
};

/* __tlregdtor: the compiler registers each of a thread's thread_local
 * objects with a destructor as it constructs it; the callbacks run, last
 * first, when the thread exits (or the module unloads, for the thread
 * that unloads it).  Up to 32 a thread, more than XAML or the STL use. */
typedef void (__cdecl *tls_dtor)(void);
static __declspec(thread) tls_dtor tls_dtors[32];
static __declspec(thread) int tls_ndtors;

int __cdecl __tlregdtor(tls_dtor fn)
{
    if (tls_ndtors == (int)(sizeof tls_dtors / sizeof tls_dtors[0]))
        return -1;
    tls_dtors[tls_ndtors++] = fn;
    return 0;
}

static VOID __stdcall tls_run_dtors(PVOID module, DWORD reason, PVOID reserved)
{
    (void)module; (void)reserved;
    if (reason != DLL_THREAD_DETACH && reason != DLL_PROCESS_DETACH)
        return;
    while (tls_ndtors > 0)
        tls_dtors[--tls_ndtors]();
}

#pragma section(".CRT$XLD", long, read)
__declspec(allocate(".CRT$XLD")) PIMAGE_TLS_CALLBACK __xl_d = tls_run_dtors;

#ifndef _WIN64
/* x86 code reaches the TLS pointer array as fs:[__tls_array] */
__asm__(".globl __tls_array\n.set __tls_array, 0x2C\n");
#endif
