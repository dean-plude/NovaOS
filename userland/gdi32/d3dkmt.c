/*
 * d3dkmt.c — gdi32's D3DKMT* thunks, the user-mode door to a WDDM display
 * driver's kernel side (adapters, devices, allocations, shared resources
 * and sync objects).  NovaOS has no WDDM kernel-mode adapters, so each
 * answers STATUS_NOT_SUPPORTED and touches nothing, as Windows does where
 * the kernel graphics subsystem is absent.  VKD3D-Proton 3.0 imports them
 * (libs/vkd3d/d3dkmt.c: it opens the adapter by LUID to share resources and
 * fences across APIs, and carries on without when that fails).
 */
#define NOVA_BUILD_GDI32
#include <windows.h>

#define STATUS_NOT_SUPPORTED ((LONG)0xC00000BB)

#define KMT(name) GDIAPI LONG name(void *args) { (void)args; return STATUS_NOT_SUPPORTED; }

KMT(D3DKMTOpenAdapterFromLuid)
KMT(D3DKMTOpenAdapterFromHdc)
KMT(D3DKMTOpenAdapterFromGdiDisplayName)
KMT(D3DKMTEnumAdapters2)
KMT(D3DKMTQueryAdapterInfo)
KMT(D3DKMTCloseAdapter)
KMT(D3DKMTCreateDevice)
KMT(D3DKMTDestroyDevice)
KMT(D3DKMTDestroyAllocation)
KMT(D3DKMTDestroyKeyedMutex)
KMT(D3DKMTDestroySynchronizationObject)
KMT(D3DKMTEscape)
KMT(D3DKMTOpenResource2)
KMT(D3DKMTOpenResourceFromNtHandle)
KMT(D3DKMTOpenSyncObjectFromNtHandle)
KMT(D3DKMTQueryResourceInfo)
KMT(D3DKMTQueryResourceInfoFromNtHandle)
KMT(D3DKMTShareObjects)
