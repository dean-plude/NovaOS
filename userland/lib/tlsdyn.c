/* tlsdyn.c — __dyn_tls_init, which constructs a thread's C++ thread_local
 * objects whose initial values are computed at run time (the MSVC CRT's
 * tlsdyn.cpp).  The compiler puts each such initializer in .CRT$XDU and
 * names __dyn_tls_init, this TLS callback, for the linker to pull in; it
 * runs them when a thread starts (and for the first thread, when the
 * program starts), once a thread.  Built into tlregdtr.lib with the
 * destructors' half (tlregdtr.c), so only a program with such objects has
 * the callback. */
#include <windows.h>
#include <winnt.h>

typedef void (__cdecl *tls_init)(void);

#pragma section(".CRT$XDA", long, read)
#pragma section(".CRT$XDZ", long, read)
__declspec(allocate(".CRT$XDA")) tls_init __xd_a = 0;
__declspec(allocate(".CRT$XDZ")) tls_init __xd_z = 0;

static __declspec(thread) int tls_inited;

void __stdcall __dyn_tls_init(PVOID module, DWORD reason, PVOID reserved)
{
    (void)module; (void)reserved;
    if ((reason != DLL_PROCESS_ATTACH && reason != DLL_THREAD_ATTACH) || tls_inited)
        return;
    tls_inited = 1;
    for (tls_init *p = &__xd_a + 1; p < &__xd_z; p++)
        if (*p)
            (*p)();
}

#pragma section(".CRT$XLC", long, read)
__declspec(allocate(".CRT$XLC")) PIMAGE_TLS_CALLBACK __xl_c = __dyn_tls_init;
