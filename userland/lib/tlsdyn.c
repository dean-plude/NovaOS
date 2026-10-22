/* tlsdyn.c — __dyn_tls_init, which constructs a thread's C++ thread_local
 * objects whose initial values are computed at run time (the MSVC CRT's
 * tlsdyn.cpp).  The compiler puts each such initializer in .CRT$XDU and
 * names __dyn_tls_init, this TLS callback, for the linker to pull in; it
 * runs them when a thread starts (and for the first thread, when the
 * program starts), once a thread.  Built into tlregdtr.lib with the
 * destructors' half (tlregdtr.c), so only a program with such objects has
 * the callback.  A compiler that guards each such object's use (clang
 * targeting MSVC 19.25 or later, as cl's /Zc:tlsGuards) tests __tls_guard
 * first and calls __dyn_tls_on_demand_init when it is still 0, for a
 * thread that started before the module was loaded. */
#include <windows.h>
#include <winnt.h>

typedef void (__cdecl *tls_init)(void);

#pragma section(".CRT$XDA", long, read)
#pragma section(".CRT$XDZ", long, read)
__declspec(allocate(".CRT$XDA")) tls_init __xd_a = 0;
__declspec(allocate(".CRT$XDZ")) tls_init __xd_z = 0;

__declspec(thread) unsigned char __tls_guard;

void __stdcall __dyn_tls_init(PVOID module, DWORD reason, PVOID reserved)
{
    (void)module; (void)reserved;
    if ((reason != DLL_PROCESS_ATTACH && reason != DLL_THREAD_ATTACH) || __tls_guard)
        return;
    __tls_guard = 1;
    for (tls_init *p = &__xd_a + 1; p < &__xd_z; p++)
        if (*p)
            (*p)();
}

void __cdecl __dyn_tls_on_demand_init(void)
{
    __dyn_tls_init(0, DLL_THREAD_ATTACH, 0);
}

#pragma section(".CRT$XLC", long, read)
__declspec(allocate(".CRT$XLC")) PIMAGE_TLS_CALLBACK __xl_c = __dyn_tls_init;
