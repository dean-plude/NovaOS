/* d3d12test — Direct3D 12 through NovaOS's d3d12.dll and VKD3D-Proton (on
 * Mesa's lavapipe Vulkan): a device, a command queue, a root signature and
 * pipeline from HLSL (NovaOS's d3dcompiler_47), a triangle drawn into a
 * render target, its pixels copied to a read-back buffer and checked, a
 * fence waited on; then the same triangle presented in a window through a
 * DXGI flip-model swap chain on the command queue, as games draw.  It runs
 * on lavapipe (VK_DRIVER_FILES), which has every Vulkan feature
 * VKD3D-Proton needs, even when Venus is installed.
 * Build: x86_64-w64-mingw32-gcc -O2 -o d3d12test.exe d3d12test.c -ld3dcompiler_47 -ldxgi -ldxguid -luser32
 *        (i686-w64-mingw32-gcc for the 32-bit one).  Usage: d3d12test [seconds]
 *        (0 seconds: the read-back only, no window)
 * d3d12.dll is loaded at run time, so the program starts (and says why it
 * cannot draw) when VKD3D-Proton is not installed. */
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 64
#define H 64

static int pass, fail;
static void check(const char *what, int ok) { if (ok) pass++; else { fail++; printf("FAIL %s\n", what); } }

static int px_near(DWORD p, int r, int g, int b)        /* p: R8G8B8A8 in memory, read as 0xAABBGGRR */
{
    return abs((int)(p & 0xFF) - r) < 8 && abs((int)(p >> 8 & 0xFF) - g) < 8 && abs((int)(p >> 16 & 0xFF) - b) < 8;
}

static const char hlsl[] =
    "float4 vs(uint id : SV_VertexID) : SV_Position {\n"
    "    float2 p[3] = { float2(0.0, 0.8), float2(0.8, -0.8), float2(-0.8, -0.8) };\n"
    "    return float4(p[id], 0.0, 1.0);\n"
    "}\n"
    "float4 ps() : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }\n";

typedef HRESULT (WINAPI *CreateDevice_t)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **);
typedef HRESULT (WINAPI *Serialize_t)(const D3D12_ROOT_SIGNATURE_DESC *, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob **, ID3DBlob **);

static ID3D12Device *dev;
static ID3D12CommandQueue *queue;
static ID3D12CommandAllocator *alloc;
static ID3D12GraphicsCommandList *list;
static ID3D12RootSignature *rootsig;
static ID3D12PipelineState *pso;
static ID3D12Fence *fence;
static UINT64 fence_value;
static HANDLE fence_event;

static ID3DBlob *compile(const char *entry, const char *target)
{
    ID3DBlob *code = NULL, *err = NULL;
    HRESULT hr = D3DCompile(hlsl, sizeof(hlsl) - 1, "d3d12test", NULL, NULL, entry, target, 0, 0, &code, &err);
    if (FAILED(hr)) printf("D3DCompile %s: 0x%08lx %s\n", entry, (unsigned long)hr,
                           err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "");
    if (err) ID3D10Blob_Release(err);
    return code;
}

static int wait_gpu(void)
{
    fence_value++;
    if (FAILED(ID3D12CommandQueue_Signal(queue, fence, fence_value))) return 0;
    if (ID3D12Fence_GetCompletedValue(fence) < fence_value) {
        ID3D12Fence_SetEventOnCompletion(fence, fence_value, fence_event);
        if (WaitForSingleObject(fence_event, 60000) != WAIT_OBJECT_0) return 0;
    }
    return ID3D12Fence_GetCompletedValue(fence) >= fence_value;
}

static void barrier(ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = { 0 };
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &b);
}

/* Record a clear to blue and the red triangle into rt */
static void draw(ID3D12Resource *rt, D3D12_CPU_DESCRIPTOR_HANDLE rtv, UINT w, UINT h)
{
    static const float blue[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    D3D12_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 };
    D3D12_RECT sc = { 0, 0, (LONG)w, (LONG)h };
    ID3D12Device_CreateRenderTargetView(dev, rt, NULL, rtv);
    ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, blue, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, rootsig);
    ID3D12GraphicsCommandList_SetPipelineState(list, pso);
    ID3D12GraphicsCommandList_RSSetViewports(list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &sc);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
}

static int setup(void)
{
    HMODULE m = LoadLibraryA("d3d12.dll");
    CreateDevice_t create = m ? (CreateDevice_t)GetProcAddress(m, "D3D12CreateDevice") : NULL;
    Serialize_t serialize = m ? (Serialize_t)GetProcAddress(m, "D3D12SerializeRootSignature") : NULL;
    check("d3d12.dll", create && serialize);
    if (!create || !serialize) return 0;
    HRESULT hr = create(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev);
    printf("D3D12CreateDevice  0x%08lx\n", (unsigned long)hr);
    check("device", SUCCEEDED(hr) && dev);
    if (!dev) return 0;

    IDXGIFactory4 *fac = NULL;
    IDXGIAdapter1 *ad = NULL;
    DXGI_ADAPTER_DESC1 ad_desc = { 0 };
    LUID luid = ID3D12Device_GetAdapterLuid(dev);
    if (SUCCEEDED(CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&fac)) &&
        SUCCEEDED(IDXGIFactory4_EnumAdapterByLuid(fac, luid, &IID_IDXGIAdapter1, (void **)&ad)) &&
        SUCCEEDED(IDXGIAdapter1_GetDesc1(ad, &ad_desc)))
        printf("D3D12 adapter  %ls\n", ad_desc.Description);
    if (ad) IDXGIAdapter1_Release(ad);
    if (fac) IDXGIFactory4_Release(fac);

    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0 };
    hr = ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&queue);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                &IID_ID3D12CommandAllocator, (void **)&alloc);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, NULL,
                                                           &IID_ID3D12GraphicsCommandList, (void **)&list);
    if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Close(list);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);
    fence_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    check("queue, command list and fence", SUCCEEDED(hr));
    if (FAILED(hr)) return 0;

    D3D12_ROOT_SIGNATURE_DESC rd = { 0, NULL, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE };
    ID3DBlob *sig = NULL, *err = NULL;
    hr = serialize(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateRootSignature(dev, 0, ID3D10Blob_GetBufferPointer(sig), ID3D10Blob_GetBufferSize(sig),
                                                             &IID_ID3D12RootSignature, (void **)&rootsig);
    if (sig) ID3D10Blob_Release(sig);
    if (err) ID3D10Blob_Release(err);
    check("root signature", SUCCEEDED(hr) && rootsig);
    if (!rootsig) return 0;

    ID3DBlob *vs = compile("vs", "vs_5_0"), *ps = compile("ps", "ps_5_0");
    check("shaders compiled", vs && ps);
    if (!vs || !ps) return 0;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd;
    memset(&pd, 0, sizeof(pd));
    pd.pRootSignature = rootsig;
    pd.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(vs); pd.VS.BytecodeLength = ID3D10Blob_GetBufferSize(vs);
    pd.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(ps); pd.PS.BytecodeLength = ID3D10Blob_GetBufferSize(ps);
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = 0xFFFFFFFF;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.SampleDesc.Count = 1;
    hr = ID3D12Device_CreateGraphicsPipelineState(dev, &pd, &IID_ID3D12PipelineState, (void **)&pso);
    printf("CreateGraphicsPipelineState  0x%08lx\n", (unsigned long)hr);
    check("pipeline state", SUCCEEDED(hr) && pso);
    ID3D10Blob_Release(vs);
    ID3D10Blob_Release(ps);
    return pso != NULL;
}

/* The triangle drawn off screen and read back */
static void test_offscreen(void)
{
    D3D12_HEAP_PROPERTIES def = { D3D12_HEAP_TYPE_DEFAULT }, rb = { D3D12_HEAP_TYPE_READBACK };
    D3D12_RESOURCE_DESC td = { 0 };
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = W; td.Height = H; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv = { DXGI_FORMAT_R8G8B8A8_UNORM, { { 0.0f, 0.0f, 1.0f, 1.0f } } };
    ID3D12Resource *rt = NULL, *buf = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource(dev, &def, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                      &cv, &IID_ID3D12Resource, (void **)&rt);
    D3D12_RESOURCE_DESC bd = { 0 };
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = W * H * 4; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateCommittedResource(dev, &rb, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
                                                                 NULL, &IID_ID3D12Resource, (void **)&buf);
    ID3D12DescriptorHeap *heap = NULL;
    D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
    if (SUCCEEDED(hr)) hr = ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&heap);
    check("render target and read-back buffer", SUCCEEDED(hr));
    if (FAILED(hr)) return;

    ID3D12CommandAllocator_Reset(alloc);
    ID3D12GraphicsCommandList_Reset(list, alloc, NULL);
    draw(rt, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap), W, H);
    barrier(rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src = { 0 }, dst = { 0 };
    src.pResource = rt; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
    dst.pResource = buf; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width = W; dst.PlacedFootprint.Footprint.Height = H;
    dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = W * 4;   /* 256: the required alignment */
    ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst, 0, 0, 0, &src, NULL);
    hr = ID3D12GraphicsCommandList_Close(list);
    check("commands recorded", SUCCEEDED(hr));
    ID3D12CommandList *lists[] = { (ID3D12CommandList *)list };
    ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
    check("fence reached", wait_gpu());

    DWORD *px = NULL;
    D3D12_RANGE r = { 0, W * H * 4 };
    hr = ID3D12Resource_Map(buf, 0, &r, (void **)&px);
    check("read-back mapped", SUCCEEDED(hr) && px);
    if (px) {
        DWORD corner = px[2 * W + 2], centre = px[(H / 2) * W + W / 2];
        printf("D3D12 pixels  corner %08lx centre %08lx\n", (unsigned long)corner, (unsigned long)centre);
        check("cleared to blue", px_near(corner, 0, 0, 0xFF));
        check("triangle drawn red", px_near(centre, 0xFF, 0, 0));
        ID3D12Resource_Unmap(buf, 0, NULL);
    }
    ID3D12DescriptorHeap_Release(heap);
    ID3D12Resource_Release(buf);
    ID3D12Resource_Release(rt);
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

/* The triangle presented in a window through a flip-model swap chain */
static void test_present(int secs)
{
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = "d3d12test";
    RegisterClassA(&wc);
    RECT wr = { 0, 0, 320, 240 };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA("d3d12test", "d3d12test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 120, 120,
                              wr.right - wr.left, wr.bottom - wr.top, 0, 0, wc.hInstance, 0);
    IDXGIFactory4 *fac = NULL;
    IDXGISwapChain1 *sc1 = NULL;
    IDXGISwapChain3 *sc = NULL;
    DXGI_SWAP_CHAIN_DESC1 sd = { 0 };
    sd.Width = 320; sd.Height = 240; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&fac);
    if (SUCCEEDED(hr)) hr = IDXGIFactory4_CreateSwapChainForHwnd(fac, (IUnknown *)queue, hwnd, &sd, NULL, NULL, &sc1);
    if (SUCCEEDED(hr)) hr = IDXGISwapChain1_QueryInterface(sc1, &IID_IDXGISwapChain3, (void **)&sc);
    printf("CreateSwapChainForHwnd  0x%08lx\n", (unsigned long)hr);
    check("swap chain on the queue", SUCCEEDED(hr) && sc);
    ID3D12DescriptorHeap *heap = NULL;
    D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
    if (sc) hr = ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&heap);
    int frames = 0, bad = 0;
    DWORD end = GetTickCount() + secs * 1000;
    while (sc && heap && GetTickCount() < end) {
        MSG msg;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        ID3D12Resource *back = NULL;
        if (FAILED(IDXGISwapChain3_GetBuffer(sc, IDXGISwapChain3_GetCurrentBackBufferIndex(sc), &IID_ID3D12Resource,
                                             (void **)&back))) { bad++; break; }
        ID3D12CommandAllocator_Reset(alloc);
        ID3D12GraphicsCommandList_Reset(list, alloc, NULL);
        barrier(back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        draw(back, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap), 320, 240);
        barrier(back, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        ID3D12GraphicsCommandList_Close(list);
        ID3D12CommandList *lists[] = { (ID3D12CommandList *)list };
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
        if (FAILED(IDXGISwapChain3_Present(sc, 1, 0))) bad++;
        else frames++;
        if (!wait_gpu()) bad++;
        ID3D12Resource_Release(back);
        if (bad) break;
    }
    printf("D3D12 presented %d frames\n", frames);
    check("frames presented", frames > 0 && !bad);
    if (sc) {                                             /* what the window shows at its centre */
        HDC dc = GetDC(hwnd);
        COLORREF s = GetPixel(dc, 160, 120);
        ReleaseDC(hwnd, dc);
        printf("D3D12 window centre  %06lx\n", (unsigned long)s);
        check("window shows the triangle", s != CLR_INVALID && GetRValue(s) > 0xF0 && GetGValue(s) < 0x10 && GetBValue(s) < 0x10);
        Sleep(1500);                                      /* (for the screenshot) */
    }
    if (heap) ID3D12DescriptorHeap_Release(heap);
    if (sc) IDXGISwapChain3_Release(sc);
    if (sc1) IDXGISwapChain1_Release(sc1);
    if (fac) IDXGIFactory4_Release(fac);
    DestroyWindow(hwnd);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int secs = argc > 1 ? atoi(argv[1]) : 3;
    char sys[MAX_PATH], lvp[MAX_PATH];
    GetSystemDirectoryA(sys, sizeof(sys));      /* (SysWOW64 for the 32-bit one) */
    snprintf(lvp, sizeof(lvp), "%s\\lvp_icd.%s.json", sys, sizeof(void *) == 8 ? "x86_64" : "x86");
    SetEnvironmentVariableA("VK_DRIVER_FILES", lvp);
    if (setup()) {
        test_offscreen();
        if (secs > 0) test_present(secs);
    }
    printf("d3d12test: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}
