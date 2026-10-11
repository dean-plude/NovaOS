/*
 * d3d12.dll — Direct3D 12, NovaOS's own front.
 *
 * NovaOS has no display adapter driver of its own.  When VKD3D-Proton is
 * installed from the App Store (its d3d12.dll as d3d12_vkd3d.dll beside
 * this one, and its d3d12core.dll), every entry point hands the call to
 * it, so Direct3D 12 runs on Vulkan: Venus (the host's GPU) or Mesa's
 * lavapipe.  Without it NovaOS answers as Windows does on a PC whose
 * display adapter has no Direct3D 12 driver: no device can be made, the
 * debug layer (a Graphics Tools component) is not installed, and
 * experimental features cannot be turned on.  Programs that import
 * d3d12.dll load and take their Direct3D 11 or software paths.  Root
 * signatures are only created on a device, so without VKD3D-Proton
 * serializing one is not provided.
 */
#include <windows.h>

#define D3D12API __declspec(dllexport)
#define DXGI_ERROR_UNSUPPORTED_            ((HRESULT)0x887A0004L)
#define DXGI_ERROR_SDK_COMPONENT_MISSING_  ((HRESULT)0x887A002DL)

static FARPROC vkd3d(const char *fn)
{
    static HMODULE m;
    static LONG tried;
    if (!InterlockedExchange(&tried, 1)) m = LoadLibraryW(L"d3d12_vkd3d.dll");
    return m ? GetProcAddress(m, fn) : NULL;
}

typedef HRESULT (WINAPI *CreateDevice_t)(void *, UINT, REFIID, void **);
typedef HRESULT (WINAPI *GetDebugInterface_t)(REFIID, void **);
typedef HRESULT (WINAPI *EnableExperimentalFeatures_t)(UINT, const IID *, void *, UINT *);
typedef HRESULT (WINAPI *GetInterface_t)(REFCLSID, REFIID, void **);
typedef HRESULT (WINAPI *SerializeRootSignature_t)(const void *, UINT, void **, void **);
typedef HRESULT (WINAPI *SerializeVersionedRootSignature_t)(const void *, void **, void **);
typedef HRESULT (WINAPI *CreateDeserializer_t)(const void *, SIZE_T, REFIID, void **);

D3D12API HRESULT WINAPI D3D12CreateDevice(void *adapter, UINT level, REFIID iid, void **device)
{
    CreateDevice_t f = (CreateDevice_t)vkd3d("D3D12CreateDevice");
    if (f) return f(adapter, level, iid, device);
    if (device) *device = NULL;
    return DXGI_ERROR_UNSUPPORTED_;
}

D3D12API HRESULT WINAPI D3D12GetDebugInterface(REFIID iid, void **debug)
{
    GetDebugInterface_t f = (GetDebugInterface_t)vkd3d("D3D12GetDebugInterface");
    if (f) return f(iid, debug);
    if (debug) *debug = NULL;
    return DXGI_ERROR_SDK_COMPONENT_MISSING_;
}

D3D12API HRESULT WINAPI D3D12EnableExperimentalFeatures(UINT n, const IID *iids, void *config, UINT *sizes)
{
    EnableExperimentalFeatures_t f = (EnableExperimentalFeatures_t)vkd3d("D3D12EnableExperimentalFeatures");
    if (f) return f(n, iids, config, sizes);
    return E_NOINTERFACE;
}

D3D12API HRESULT WINAPI D3D12GetInterface(REFCLSID clsid, REFIID iid, void **out)
{
    GetInterface_t f = (GetInterface_t)vkd3d("D3D12GetInterface");
    if (f) return f(clsid, iid, out);
    if (out) *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT no_root_signature(void **blob, void **error)
{
    if (blob) *blob = NULL;
    if (error) *error = NULL;
    return E_NOTIMPL;
}
D3D12API HRESULT WINAPI D3D12SerializeRootSignature(const void *desc, UINT version, void **blob, void **error)
{
    SerializeRootSignature_t f = (SerializeRootSignature_t)vkd3d("D3D12SerializeRootSignature");
    if (f) return f(desc, version, blob, error);
    return no_root_signature(blob, error);
}
D3D12API HRESULT WINAPI D3D12SerializeVersionedRootSignature(const void *desc, void **blob, void **error)
{
    SerializeVersionedRootSignature_t f = (SerializeVersionedRootSignature_t)vkd3d("D3D12SerializeVersionedRootSignature");
    if (f) return f(desc, blob, error);
    return no_root_signature(blob, error);
}
D3D12API HRESULT WINAPI D3D12CreateRootSignatureDeserializer(const void *data, SIZE_T n, REFIID iid, void **out)
{
    CreateDeserializer_t f = (CreateDeserializer_t)vkd3d("D3D12CreateRootSignatureDeserializer");
    if (f) return f(data, n, iid, out);
    if (out) *out = NULL;
    return E_NOTIMPL;
}
D3D12API HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(const void *data, SIZE_T n, REFIID iid, void **out)
{
    CreateDeserializer_t f = (CreateDeserializer_t)vkd3d("D3D12CreateVersionedRootSignatureDeserializer");
    if (f) return f(data, n, iid, out);
    if (out) *out = NULL;
    return E_NOTIMPL;
}
