/*
 * dbgeng.dll — the Debugger Engine
 *
 * Programs that print their own stack trace when they crash (qBittorrent)
 * import DebugCreate and call it only then, to name the frames' symbols.
 * NovaOS has no debugger engine (its own crash report in the serial log
 * names each frame's module and offset), so DebugCreate says so and the
 * program falls back to plain addresses.
 */

#include <windows.h>

#define EXPORT __declspec(dllexport)

EXPORT HRESULT WINAPI DebugCreate(REFIID iid, void **out)
{
    (void)iid;
    if (!out) return E_POINTER;
    *out = NULL;
    return E_NOINTERFACE;
}

EXPORT HRESULT WINAPI DebugConnect(LPCSTR remote, REFIID iid, void **out)
{
    (void)remote; (void)iid;
    if (!out) return E_POINTER;
    *out = NULL;
    return E_NOINTERFACE;
}

EXPORT HRESULT WINAPI DebugConnectWide(LPCWSTR remote, REFIID iid, void **out)
{
    (void)remote; (void)iid;
    if (!out) return E_POINTER;
    *out = NULL;
    return E_NOINTERFACE;
}
