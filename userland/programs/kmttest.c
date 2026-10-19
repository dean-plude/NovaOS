/*
 * kmttest.exe — the D3DKMT* calls on NovaOS's Basic Render adapter (step 3
 * of docs/design/intel-gpu.md, its first part: kernel/um/um_d3dkmt.c):
 *
 *   adapters      D3DKMTEnumAdapters2 lists the adapter with its LUID;
 *                 D3DKMTOpenAdapterFromLuid finds it again and refuses an
 *                 unknown LUID; D3DKMTQueryAdapterInfo says render-only,
 *                 software, WDDM 2.0, and its shared memory
 *   allocations   D3DKMTCreateAllocation2 gives memory the video memory
 *                 counts (D3DKMTQueryVideoMemoryInfo); D3DKMTLock2 maps it
 *                 read-write and a second lock sees what the first wrote;
 *                 too big is refused; D3DKMTDestroyAllocation2 gives it back
 *   fences        a monitored fence's value is on a read-only page;
 *                 D3DKMTSignalSynchronizationObjectFromCpu raises it (never
 *                 lowers it); D3DKMTWaitForSynchronizationObjectFromCpu
 *                 returns at once when reached, sets an event later when not,
 *                 and holds another thread until the value comes
 *   handles       a bad or closed handle is refused; destroying the device
 *                 takes its fences with it
 *
 * "kmttest create" registers a root-enumerated display device (hardware ID
 * root\WddmTest, as devcon install does before it installs a package), for
 * wddmtest.sys, NovaOS's test render miniport (tools/drivertest/
 * wddmtest.c), and "kmttest wddm" checks the adapter that driver makes,
 * through the same calls (step 3's second part: kernel/nt/dxgkrnl.c):
 *
 *   adapter       listed beside Basic Render, a render-only hardware one
 *                 (not software), WDDM 2.0, its one segment of system
 *                 memory as the driver describes it (DXGKQAITYPE_QUERYSEGMENT4)
 *   allocations   sized by the driver (DxgkDdiCreateAllocation: it rounds to
 *                 its 64 KiB GPU pages) and opened on the device
 *                 (DxgkDdiOpenAllocation); counted, mapped by Lock2, given
 *                 back; one past the segment refused; DestroyDevice takes the
 *                 rest with it (DxgkDdiCloseAllocation, DestroyAllocation,
 *                 DestroyDevice)
 *   GPU VA        (step 3's third part) a paging queue and its fence; an
 *                 allocation mapped where VidMm picks and into a reserved
 *                 range, overlaps and overruns refused, the paging fence
 *                 already reached; MakeResident and Evict counted; freeing
 *                 the range and destroying the allocation unmap them (the
 *                 driver reads each mapping's first word back through its
 *                 page tables, which the self-test checks in its log)
 *   submission    (step 3's fourth part) a context on the driver's one node
 *                 (another node refused); DMA buffers in wddmtest's command
 *                 format written through Lock2 and submitted at their GPU
 *                 addresses: the GPU writes and copies through the page
 *                 tables and writes a monitored fence at its GPU address;
 *                 SignalSynchronizationObjectFromGpu2 raises a fence once the
 *                 work before it is done; WaitForSynchronizationObjectFromGpu
 *                 holds the context's later work until the CPU signals; a
 *                 write to a read-only mapping faults: the context is lost
 *                 (its fence raised to the maximum, its next submission
 *                 refused); a destroyed context's handle is refused
 *   preemption,   (step 3's fifth part) a context whose buffer spins on the
 *   TDR           GPU (wddmtest's SPIN) while another context's work waits
 *                 is preempted, the other's runs, and destroying the
 *                 spinning one does not hang; a spin the GPU cannot stop is
 *                 reset after the timeout: that context is lost (its fence
 *                 raised to the maximum, STATUS_DEVICE_REMOVED after) and the
 *                 other context goes on
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <d3dkmthk.h>

#define ST_INVALID_PARAMETER ((LONG)0xC000000D)
#define ST_DEVICE_REMOVED    ((LONG)0xC00002B6)

static int g_pass, g_fail;

static void check(int ok, const char *what)
{
    if (ok) g_pass++;
    else { g_fail++; printf("FAIL: %s\n", what); }
}

static D3DKMT_HANDLE g_dev, g_fence;

static DWORD WINAPI waiter(void *arg)
{
    UINT64 v = (UINT64)(ULONG_PTR)arg;
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU w = { g_dev, 1, &g_fence, &v, NULL, 0 };
    return (DWORD)D3DKMTWaitForSynchronizationObjectFromCpu(&w);
}

static D3DKMT_HANDLE allocate(D3DKMT_HANDLE dev, UINT64 size, LONG *st)
{
    NOVA_KMT_ALLOCATION spec = { size, 0, 0 };
    D3DDDI_ALLOCATIONINFO2 info;
    memset(&info, 0, sizeof(info));
    info.pPrivateDriverData = &spec;
    info.PrivateDriverDataSize = sizeof(spec);
    D3DKMT_CREATEALLOCATION ca;
    memset(&ca, 0, sizeof(ca));
    ca.hDevice = dev;
    ca.NumAllocations = 1;
    ca.pAllocationInfo2 = &info;
    *st = D3DKMTCreateAllocation2(&ca);
    return *st ? 0 : info.hAllocation;
}

static UINT64 usage(D3DKMT_HANDLE adapter)
{
    D3DKMT_QUERYVIDEOMEMORYINFO q;
    memset(&q, 0, sizeof(q));
    q.hAdapter = adapter;
    return D3DKMTQueryVideoMemoryInfo(&q) ? (UINT64)-1 : q.CurrentUsage;
}

/* ---- "kmttest create": the root-enumerated device wddmtest.sys installs on ---- */
/* (setupapi's and cfgmgr32's, declared here as vhidtest.c does) */
typedef DWORD CONFIGRET;
__declspec(dllimport) CONFIGRET WINAPI CM_Locate_DevNodeW(PDWORD dn, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_DevNode_Status(PULONG status, PULONG problem, DWORD dn, ULONG flags);
__declspec(dllimport) HANDLE WINAPI SetupDiGetClassDevsW(const GUID *cls, LPCWSTR enumerator, HWND parent, DWORD flags);
__declspec(dllimport) HANDLE WINAPI SetupDiCreateDeviceInfoList(const GUID *cls, HWND parent);
__declspec(dllimport) BOOL WINAPI SetupDiCreateDeviceInfoW(HANDLE set, LPCWSTR name, const GUID *cls, LPCWSTR desc, HWND parent,
                                                           DWORD flags, PVOID dev);
__declspec(dllimport) BOOL WINAPI SetupDiSetDeviceRegistryPropertyW(HANDLE set, PVOID dev, DWORD prop, const BYTE *buf, DWORD size);
__declspec(dllimport) BOOL WINAPI SetupDiGetDeviceRegistryPropertyW(HANDLE set, PVOID dev, DWORD prop, PDWORD type, PBYTE buf,
                                                                    DWORD n, PDWORD need);
__declspec(dllimport) BOOL WINAPI SetupDiCallClassInstaller(DWORD dif, HANDLE set, PVOID dev);
__declspec(dllimport) BOOL WINAPI SetupDiGetDeviceInstanceIdW(HANDLE set, PVOID dev, LPWSTR id, DWORD n, PDWORD need);
__declspec(dllimport) BOOL WINAPI SetupDiEnumDeviceInfo(HANDLE set, DWORD i, PVOID info);
__declspec(dllimport) BOOL WINAPI SetupDiDestroyDeviceInfoList(HANDLE set);
typedef struct { DWORD cbSize; GUID ClassGuid; DWORD DevInst; ULONG_PTR Reserved; } SP_DEVINFO_DATA;
#define DICD_GENERATE_ID      0x01
#define DIF_REGISTERDEVICE    0x19
#define SPDRP_HARDWAREID      0x01

#define WDDMTEST_HWID L"root\\WddmTest"
static const GUID GUID_DEVCLASS_DISPLAY_ = { 0x4d36e968, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };

static BOOL find_node(WCHAR *id, int cap)
{
    HANDLE set = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY_, L"ROOT", NULL, 0);
    if (set == INVALID_HANDLE_VALUE) return FALSE;
    BOOL found = FALSE;
    for (DWORD i = 0; !found; i++) {
        SP_DEVINFO_DATA d = { sizeof(d) };
        if (!SetupDiEnumDeviceInfo(set, i, &d)) break;
        WCHAR hw[200];
        memset(hw, 0, sizeof(hw));
        if (!SetupDiGetDeviceRegistryPropertyW(set, &d, SPDRP_HARDWAREID, NULL, (PBYTE)hw, sizeof(hw) - 4, NULL)) continue;
        if (_wcsicmp(hw, WDDMTEST_HWID)) continue;
        found = SetupDiGetDeviceInstanceIdW(set, &d, id, cap, NULL);
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

static int create(void)
{
    WCHAR id[200];
    if (find_node(id, 200)) printf("kmttest: %ls is registered already\n", id);
    else {
        HANDLE set = SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_DISPLAY_, NULL);
        SP_DEVINFO_DATA d = { sizeof(d) };
        static const WCHAR hw[] = WDDMTEST_HWID L"\0";
        BOOL ok = set != INVALID_HANDLE_VALUE &&
                  SetupDiCreateDeviceInfoW(set, L"Display", &GUID_DEVCLASS_DISPLAY_, NULL, NULL, DICD_GENERATE_ID, &d) &&
                  SetupDiSetDeviceRegistryPropertyW(set, &d, SPDRP_HARDWAREID, (const BYTE *)hw, sizeof(hw)) &&
                  SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set, &d);
        if (!ok) printf("kmttest: registering failed, error %lu\n", GetLastError());
        check(ok, "a root-enumerated display device registered (SetupDiCreateDeviceInfo, DIF_REGISTERDEVICE)");
        if (ok && SetupDiGetDeviceInstanceIdW(set, &d, id, 200, NULL)) printf("kmttest: registered %ls\n", id);
        if (set != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(set);
    }
    DWORD dn;
    ULONG status = 0, problem = 0;
    check(find_node(id, 200) && !CM_Locate_DevNodeW(&dn, id, 0) && !CM_Get_DevNode_Status(&status, &problem, dn, 0),
          "its device node is there");
    printf("kmttest create: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}

/* ---- "kmttest wddm", its GPU virtual addresses (the driver's GPU MMU) ---- */
#define WORD_A 0x600DF00Du
#define WORD_B 0x5ECD0A6Eu

static void gpu_va(D3DKMT_HANDLE adapter, D3DKMT_HANDLE dev)
{
    LONG st;
    D3DKMT_CREATEPAGINGQUEUE pq;
    memset(&pq, 0, sizeof(pq));
    pq.hDevice = dev;
    check(D3DKMTCreatePagingQueue(&pq) == 0 && pq.hPagingQueue && pq.hSyncObject && pq.FenceValueCPUVirtualAddress,
          "CreatePagingQueue: a paging queue and its monitored fence");
    if (!pq.FenceValueCPUVirtualAddress) return;
    volatile UINT64 *fence = pq.FenceValueCPUVirtualAddress;
    D3DKMT_HANDLE a3 = allocate(dev, 128u << 10, &st);
    check(a3 != 0, "a 128 KiB allocation to map");
    D3DKMT_LOCK2 lk = { dev, a3, 0, NULL };
    if (D3DKMTLock2(&lk) == 0 && lk.pData) {
        ((volatile UINT *)lk.pData)[0] = WORD_A;
        ((volatile UINT *)((char *)lk.pData + 65536))[0] = WORD_B;
        D3DKMT_UNLOCK2 ul = { dev, a3 };
        D3DKMTUnlock2(&ul);
    }

    D3DDDI_MAPGPUVIRTUALADDRESS m;
    memset(&m, 0, sizeof(m));
    m.hPagingQueue = pq.hPagingQueue;
    m.hAllocation = a3;
    m.SizeInPages = 32;
    m.Protection.Write = 1;
    st = D3DKMTMapGpuVirtualAddress(&m);
    check(st == 0 && m.VirtualAddress && !(m.VirtualAddress & 0xFFFF),
          "MapGpuVirtualAddress: VidMm picks a 64 KiB-aligned address, the driver writes the page tables");
    printf("kmttest: mapped at GPU VA 0x%llx (0x%08lx)\n", (unsigned long long)m.VirtualAddress, (unsigned long)st);
    check(m.PagingFenceValue && *fence >= m.PagingFenceValue, "its paging fence value is already reached");

    D3DDDI_RESERVEGPUVIRTUALADDRESS r;
    memset(&r, 0, sizeof(r));
    r.hPagingQueue = pq.hPagingQueue;
    r.Size = 1u << 20;
    check(D3DKMTReserveGpuVirtualAddress(&r) == 0 && r.VirtualAddress &&
          (r.VirtualAddress >= m.VirtualAddress + (128u << 10) || r.VirtualAddress + r.Size <= m.VirtualAddress),
          "ReserveGpuVirtualAddress: 1 MiB apart from the mapping");
    D3DDDI_MAPGPUVIRTUALADDRESS m2 = m;
    m2.BaseAddress = r.VirtualAddress + 0x10000;
    m2.OffsetInPages = 16;
    m2.SizeInPages = 16;
    m2.Protection.Value = 0;                              /* read-only */
    m2.VirtualAddress = 0;
    check(D3DKMTMapGpuVirtualAddress(&m2) == 0 && m2.VirtualAddress == r.VirtualAddress + 0x10000 &&
          m2.PagingFenceValue > m.PagingFenceValue,
          "MapGpuVirtualAddress in the reserved range: the second half, read-only");
    D3DDDI_MAPGPUVIRTUALADDRESS bad = m2;
    bad.BaseAddress = r.VirtualAddress + 0x8000;
    check(D3DKMTMapGpuVirtualAddress(&bad) != 0, "a mapping over part of another is refused");
    bad = m;
    bad.SizeInPages = 33;
    check(D3DKMTMapGpuVirtualAddress(&bad) != 0, "one past the allocation's end is refused");

    D3DDDI_MAKERESIDENT mr;
    memset(&mr, 0, sizeof(mr));
    mr.hPagingQueue = pq.hPagingQueue;
    mr.NumAllocations = 1;
    mr.AllocationList = &a3;
    check(D3DKMTMakeResident(&mr) == 0 && mr.PagingFenceValue && *fence >= mr.PagingFenceValue, "MakeResident");
    UINT64 before = usage(adapter);
    D3DKMT_EVICT ev;
    memset(&ev, 0, sizeof(ev));
    ev.hDevice = dev;
    ev.NumAllocations = 1;
    ev.AllocationList = &a3;
    check(D3DKMTEvict(&ev) == 0 && usage(adapter) == before, "an Evict after a MakeResident leaves it resident");
    check(D3DKMTEvict(&ev) == 0 && usage(adapter) == before - (128u << 10), "a second Evict: its memory no longer counted");
    check(D3DKMTMakeResident(&mr) == 0 && usage(adapter) == before, "MakeResident counts it again");

    D3DKMT_FREEGPUVIRTUALADDRESS fr = { adapter, r.VirtualAddress, r.Size };
    check(D3DKMTFreeGpuVirtualAddress(&fr) == 0, "FreeGpuVirtualAddress unmaps and frees the reserved range");
    check(D3DKMTFreeGpuVirtualAddress(&fr) != 0, "freeing it twice is refused");
    D3DKMT_DESTROYALLOCATION2 da = { dev, 0, &a3, 1, 0 };
    check(D3DKMTDestroyAllocation2(&da) == 0, "destroying a mapped allocation unmaps it");
    D3DDDI_DESTROYPAGINGQUEUE dq = { pq.hPagingQueue };
    check(D3DKMTDestroyPagingQueue(&dq) == 0, "DestroyPagingQueue");
}

/* ---- "kmttest wddm": contexts and submission (the driver's software GPU) ---- */
#define GPU_WRITE 1u
#define GPU_COPY  2u
#define GPU_FENCE 3u
#define GPU_SPIN  4u

static UINT *put_write(UINT *w, UINT64 va, UINT v) { *w++ = GPU_WRITE; *w++ = (UINT)va; *w++ = (UINT)(va >> 32); *w++ = v; return w; }

static UINT *put_copy(UINT *w, UINT64 src, UINT64 dst, UINT n)
{
    *w++ = GPU_COPY; *w++ = (UINT)src; *w++ = (UINT)(src >> 32); *w++ = (UINT)dst; *w++ = (UINT)(dst >> 32); *w++ = n;
    return w;
}

static UINT *put_fence(UINT *w, UINT64 va, UINT64 v)
{
    *w++ = GPU_FENCE; *w++ = (UINT)va; *w++ = (UINT)(va >> 32); *w++ = (UINT)v; *w++ = (UINT)(v >> 32);
    return w;
}

static UINT64 map(D3DKMT_HANDLE pq, D3DKMT_HANDLE alloc, UINT pages, int write)
{
    D3DDDI_MAPGPUVIRTUALADDRESS m;
    memset(&m, 0, sizeof(m));
    m.hPagingQueue = pq;
    m.hAllocation = alloc;
    m.SizeInPages = pages;
    m.Protection.Write = write;
    return D3DKMTMapGpuVirtualAddress(&m) ? 0 : m.VirtualAddress;
}

static LONG submit(D3DKMT_HANDLE ctx, UINT64 va, UINT bytes)
{
    D3DKMT_SUBMITCOMMAND sc;
    memset(&sc, 0, sizeof(sc));
    sc.Commands = va;
    sc.CommandLength = bytes;
    sc.BroadcastContextCount = 1;
    sc.BroadcastContext[0] = ctx;
    return D3DKMTSubmitCommand(&sc);
}

static LONG signal_gpu(D3DKMT_HANDLE ctx, D3DKMT_HANDLE fence, UINT64 v)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 s;
    memset(&s, 0, sizeof(s));
    s.ObjectCount = 1;
    s.ObjectHandleArray = &fence;
    s.BroadcastContextCount = 1;
    s.BroadcastContextArray = &ctx;
    s.MonitoredFenceValueArray = &v;
    return D3DKMTSignalSynchronizationObjectFromGpu2(&s);
}

/* Whether @fence reaches @v within @ms (a wait that sets an event: a broken GPU cannot hang the test) */
static int reaches(D3DKMT_HANDLE dev, D3DKMT_HANDLE fence, UINT64 v, DWORD ms)
{
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU w = { dev, 1, &fence, &v, ev, 0 };
    int ok = D3DKMTWaitForSynchronizationObjectFromCpu(&w) == 0 && WaitForSingleObject(ev, ms) == WAIT_OBJECT_0;
    CloseHandle(ev);
    return ok;
}

static D3DKMT_HANDLE monitored(D3DKMT_HANDLE dev, volatile UINT64 **cpu, UINT64 *gpu)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 cs;
    memset(&cs, 0, sizeof(cs));
    cs.hDevice = dev;
    cs.Info.Type = D3DDDI_MONITORED_FENCE;
    if (D3DKMTCreateSynchronizationObject2(&cs)) return 0;
    *cpu = cs.Info.MonitoredFence.FenceValueCPUVirtualAddress;
    *gpu = cs.Info.MonitoredFence.FenceValueGPUVirtualAddress;
    return cs.hSyncObject;
}

static void submission(D3DKMT_HANDLE dev)
{
    LONG st;
    D3DKMT_CREATEPAGINGQUEUE pq;
    memset(&pq, 0, sizeof(pq));
    pq.hDevice = dev;
    check(D3DKMTCreatePagingQueue(&pq) == 0, "a paging queue for the buffers' mappings");
    D3DKMT_HANDLE cmd = allocate(dev, 65536, &st), dst = allocate(dev, 65536, &st);
    UINT64 cva = map(pq.hPagingQueue, cmd, 16, 1), dva = map(pq.hPagingQueue, dst, 16, 1), rova = map(pq.hPagingQueue, dst, 16, 0);
    check(cmd && dst && cva && dva && rova, "a command buffer and a target mapped (the target twice, once read-only)");
    volatile UINT64 *fcpu = NULL, *f2cpu = NULL;
    UINT64 fgpu = 0, f2gpu = 0;
    D3DKMT_HANDLE fence = monitored(dev, &fcpu, &fgpu), fence2 = monitored(dev, &f2cpu, &f2gpu);
    check(fence && fence2 && fcpu && fgpu && f2gpu && fgpu != f2gpu,
          "monitored fences have GPU virtual addresses (their value pages, mapped for the GPU)");

    D3DKMT_CREATECONTEXTVIRTUAL cc;
    memset(&cc, 0, sizeof(cc));
    cc.hDevice = dev;
    cc.NodeOrdinal = 1;
    check(D3DKMTCreateContextVirtual(&cc) != 0, "a context on a node the GPU does not have is refused");
    cc.NodeOrdinal = 0;
    cc.ClientHint = D3DKMT_CLIENTHINT_VULKAN;
    check(D3DKMTCreateContextVirtual(&cc) == 0 && cc.hContext, "CreateContextVirtual (DxgkDdiCreateContext, SetRootPageTable)");
    D3DKMT_HANDLE ctx = cc.hContext;

    D3DKMT_LOCK2 lk = { dev, cmd, 0, NULL }, ld = { dev, dst, 0, NULL };
    UINT *cw = NULL, *dw = NULL;
    if (D3DKMTLock2(&lk) == 0) cw = lk.pData;
    if (D3DKMTLock2(&ld) == 0) dw = ld.pData;
    check(cw && dw, "both locked for the CPU");
    if (!cw || !dw || !fcpu || !ctx) return;
    for (int i = 0; i < 8; i++) cw[0x3000 / 4 + i] = 0xA0000000u + (UINT)i;   /* (what the GPU copies) */

    /* a buffer: a write, a copy, then the fence written at its GPU address */
    UINT *w = put_write(cw, dva, 0xC0FFEE01u);
    w = put_copy(w, cva + 0x3000, dva + 0x100, 8);
    w = put_fence(w, fgpu, 5);
    check(submit(ctx, cva, (UINT)((char *)w - (char *)cw)) == 0, "SubmitCommand (DxgkDdiSubmitCommandVirtual)");
    check(reaches(dev, fence, 5, 5000) && *fcpu == 5, "the GPU wrote the fence: the CPU's wait for 5 returns");
    check(dw[0] == 0xC0FFEE01u, "the GPU's write landed in the target, through the page tables");
    check(dw[0x40] == 0xA0000000u && dw[0x47] == 0xA0000007u, "and its copy");

    /* a buffer, then a fence the scheduler signals once it is done */
    w = put_write(cw + 0x1000 / 4, dva + 4, 0x5161A1EDu);
    check(submit(ctx, cva + 0x1000, (UINT)((char *)w - (char *)(cw + 0x1000 / 4))) == 0 && signal_gpu(ctx, fence, 6) == 0,
          "SubmitCommand and SignalSynchronizationObjectFromGpu2");
    check(reaches(dev, fence, 6, 5000) && dw[1] == 0x5161A1EDu, "the fence rose after the buffer ran");

    /* a wait from the GPU holds what comes after it */
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU wg;
    memset(&wg, 0, sizeof(wg));
    UINT64 one = 1;
    wg.hContext = ctx;
    wg.ObjectCount = 1;
    wg.ObjectHandleArray = &fence2;
    wg.MonitoredFenceValueArray = &one;
    check(D3DKMTWaitForSynchronizationObjectFromGpu(&wg) == 0, "WaitForSynchronizationObjectFromGpu");
    w = put_write(cw + 0x2000 / 4, dva + 8, 0x3A17EDu);
    check(submit(ctx, cva + 0x2000, (UINT)((char *)w - (char *)(cw + 0x2000 / 4))) == 0 && signal_gpu(ctx, fence, 7) == 0,
          "work queued behind the wait");
    check(!reaches(dev, fence, 7, 300) && dw[2] == 0, "it waits for the other fence");
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU sg = { dev, 1, &fence2, &one, 0 };
    check(D3DKMTSignalSynchronizationObjectFromCpu(&sg) == 0 && reaches(dev, fence, 7, 5000) && dw[2] == 0x3A17EDu,
          "signalled from the CPU, the context goes on");

    /* a write to the read-only mapping: a page fault, which loses the context */
    w = put_write(cw + 0x2800 / 4, rova + 12, 0xBADu);
    check(submit(ctx, cva + 0x2800, (UINT)((char *)w - (char *)(cw + 0x2800 / 4))) == 0 && signal_gpu(ctx, fence, 8) == 0 &&
          reaches(dev, fence, 8, 5000) && dw[3] == 0, "a write to a read-only page faults and writes nothing");
    check(*fcpu == ~0ull, "the faulting context is lost: the fence it would signal is raised to the maximum");
    check(submit(ctx, cva, 16) == ST_DEVICE_REMOVED, "its next submission fails (STATUS_DEVICE_REMOVED)");

    D3DKMT_UNLOCK2 ul = { dev, cmd }, ud = { dev, dst };
    D3DKMTUnlock2(&ul);
    D3DKMTUnlock2(&ud);
    D3DKMT_DESTROYCONTEXT dc = { ctx };
    check(D3DKMTDestroyContext(&dc) == 0, "DestroyContext (DxgkDdiDestroyContext)");
    check(D3DKMTDestroyContext(&dc) != 0 && submit(ctx, cva, 16) != 0, "its handle is refused after");
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT ds = { fence };
    D3DKMTDestroySynchronizationObject(&ds);
    ds.hSyncObject = fence2;
    D3DKMTDestroySynchronizationObject(&ds);
    D3DKMT_HANDLE both[2] = { cmd, dst };
    D3DKMT_DESTROYALLOCATION2 da = { dev, 0, both, 2, 0 };
    check(D3DKMTDestroyAllocation2(&da) == 0, "the buffers given back");
    D3DDDI_DESTROYPAGINGQUEUE dq = { pq.hPagingQueue };
    D3DKMTDestroyPagingQueue(&dq);
}

/* ---- "kmttest wddm": preemption and TDR (wddmtest's SPIN command) ---- */
static UINT *put_spin(UINT *w, UINT flags) { *w++ = GPU_SPIN; *w++ = flags; return w; }

static D3DKMT_HANDLE context(D3DKMT_HANDLE dev)
{
    D3DKMT_CREATECONTEXTVIRTUAL cc;
    memset(&cc, 0, sizeof(cc));
    cc.hDevice = dev;
    cc.ClientHint = D3DKMT_CLIENTHINT_VULKAN;
    return D3DKMTCreateContextVirtual(&cc) ? 0 : cc.hContext;
}

static void tdr(D3DKMT_HANDLE dev)
{
    LONG st;
    D3DKMT_CREATEPAGINGQUEUE pq;
    memset(&pq, 0, sizeof(pq));
    pq.hDevice = dev;
    D3DKMTCreatePagingQueue(&pq);
    D3DKMT_HANDLE cmd = allocate(dev, 65536, &st), dst = allocate(dev, 65536, &st);
    UINT64 cva = map(pq.hPagingQueue, cmd, 16, 1), dva = map(pq.hPagingQueue, dst, 16, 1);
    volatile UINT64 *fa_cpu = NULL, *fb_cpu = NULL, *fc_cpu = NULL;
    UINT64 fa_gpu, fb_gpu, fc_gpu;
    D3DKMT_HANDLE fa = monitored(dev, &fa_cpu, &fa_gpu), fb = monitored(dev, &fb_cpu, &fb_gpu),
                  fc = monitored(dev, &fc_cpu, &fc_gpu);
    D3DKMT_HANDLE spin = context(dev), other = context(dev);
    D3DKMT_LOCK2 lk = { dev, cmd, 0, NULL }, ld = { dev, dst, 0, NULL };
    UINT *cw = NULL, *dw = NULL;
    if (D3DKMTLock2(&lk) == 0) cw = lk.pData;
    if (D3DKMTLock2(&ld) == 0) dw = ld.pData;
    check(cmd && dst && cva && dva && fa && fb && fc && spin && other && cw && dw,
          "two contexts, buffers and fences for the preemption checks");
    if (!cw || !dw || !spin || !other || !fc_cpu) return;

    /* a spin the GPU can stop: another context's work preempts it */
    UINT *w = put_spin(cw, 1);
    w = put_write(w, dva, 0xA11u);
    check(submit(spin, cva, (UINT)((char *)w - (char *)cw)) == 0 && signal_gpu(spin, fa, 1) == 0,
          "a buffer that spins until preempted");
    Sleep(100);
    w = put_write(cw + 0x1000 / 4, dva + 4, 0xB0Bu);
    check(submit(other, cva + 0x1000, (UINT)((char *)w - (char *)(cw + 0x1000 / 4))) == 0 && signal_gpu(other, fb, 1) == 0,
          "another context's work queued behind it");
    check(reaches(dev, fb, 1, 10000) && dw[1] == 0xB0Bu, "the spin is preempted (DxgkDdiPreemptCommand): the other runs");
    check(!reaches(dev, fa, 1, 300) && dw[0] == 0, "the preempted context is given the GPU again and spins on");
    D3DKMT_DESTROYCONTEXT dc = { spin };
    DWORD t0 = GetTickCount();
    check(D3DKMTDestroyContext(&dc) == 0 && GetTickCount() - t0 < 4000, "destroying it preempts its spin: no wait for a TDR");

    /* a spin the GPU cannot stop: the timeout resets the engine */
    spin = context(dev);
    w = put_spin(cw + 0x2000 / 4, 0);
    check(spin && submit(spin, cva + 0x2000, (UINT)((char *)w - (char *)(cw + 0x2000 / 4))) == 0 &&
          signal_gpu(spin, fc, 1) == 0, "a buffer that spins until the GPU is reset");
    Sleep(100);
    w = put_write(cw + 0x3000 / 4, dva + 8, 0xB0B2u);
    check(submit(other, cva + 0x3000, (UINT)((char *)w - (char *)(cw + 0x3000 / 4))) == 0 && signal_gpu(other, fb, 2) == 0,
          "the other context's work queued behind it");
    check(reaches(dev, fb, 2, 15000) && dw[2] == 0xB0B2u,
          "no preemption in 2 s: the engine is reset (DxgkDdiResetEngine) and the other context's work runs");
    check(reaches(dev, fc, 1, 1000) && *fc_cpu == ~0ull, "the hung context is lost: its fence is raised to the maximum");
    check(submit(spin, cva + 0x2000, 8) == ST_DEVICE_REMOVED && signal_gpu(spin, fc, 2) == ST_DEVICE_REMOVED,
          "its later calls fail (STATUS_DEVICE_REMOVED)");
    w = put_write(cw + 0x3800 / 4, dva + 12, 0xB0B3u);
    check(submit(other, cva + 0x3800, (UINT)((char *)w - (char *)(cw + 0x3800 / 4))) == 0 && signal_gpu(other, fb, 3) == 0 &&
          reaches(dev, fb, 3, 5000) && dw[3] == 0xB0B3u, "the other context goes on");
    dc.hContext = spin;
    check(D3DKMTDestroyContext(&dc) == 0, "the lost context is destroyed");
    dc.hContext = other;
    D3DKMTDestroyContext(&dc);

    D3DKMT_UNLOCK2 ul = { dev, cmd }, ud = { dev, dst };
    D3DKMTUnlock2(&ul);
    D3DKMTUnlock2(&ud);
    D3DKMT_HANDLE fences[3] = { fa, fb, fc };
    for (int i = 0; i < 3; i++) {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT ds = { fences[i] };
        D3DKMTDestroySynchronizationObject(&ds);
    }
    D3DKMT_HANDLE both[2] = { cmd, dst };
    D3DKMT_DESTROYALLOCATION2 da = { dev, 0, both, 2, 0 };
    D3DKMTDestroyAllocation2(&da);
    D3DDDI_DESTROYPAGINGQUEUE dq = { pq.hPagingQueue };
    D3DKMTDestroyPagingQueue(&dq);
}

/* ---- "kmttest wddm": the adapter of wddmtest.sys ---- */
static int wddm(void)
{
    LONG st;
    D3DKMT_ADAPTERINFO info[8];
    D3DKMT_ENUMADAPTERS2 en = { 8, info };
    st = D3DKMTEnumAdapters2(&en);
    check(st == 0 && en.NumAdapters >= 2, "EnumAdapters2 lists the driver's adapter beside Basic Render");
    D3DKMT_HANDLE adapter = 0, basic = 0;
    for (UINT i = 0; st == 0 && i < en.NumAdapters; i++) {
        D3DKMT_ADAPTERTYPE type = { 0 };
        D3DKMT_QUERYADAPTERINFO qa = { info[i].hAdapter, KMTQAITYPE_ADAPTERTYPE, &type, sizeof(type) };
        if (D3DKMTQueryAdapterInfo(&qa)) continue;
        if (type.Value & D3DKMT_ADAPTERTYPE_SOFTWARE_DEVICE) basic = info[i].hAdapter;
        else if (!adapter && (type.Value & D3DKMT_ADAPTERTYPE_RENDER_SUPPORTED) &&
                 !(type.Value & D3DKMT_ADAPTERTYPE_DISPLAY_SUPPORTED)) {
            adapter = info[i].hAdapter;
            printf("kmttest: render adapter LUID %08lx:%08lx\n", (unsigned long)info[i].AdapterLuid.HighPart,
                   (unsigned long)info[i].AdapterLuid.LowPart);
        }
    }
    check(basic != 0, "Basic Render is still listed");
    check(adapter != 0, "a render-only adapter that is not software (the driver's)");
    if (!adapter) { printf("kmttest wddm: %d passed, %d failed\n", g_pass, g_fail); return 1; }

    D3DKMT_DRIVERVERSION ver = 0;
    D3DKMT_QUERYADAPTERINFO qa = { adapter, KMTQAITYPE_DRIVERVERSION, &ver, sizeof(ver) };
    check(D3DKMTQueryAdapterInfo(&qa) == 0 && ver >= KMT_DRIVERVERSION_WDDM_2_0, "its driver says WDDM 2.0 or later");
    D3DKMT_SEGMENTSIZEINFO seg;
    memset(&seg, 0, sizeof(seg));
    qa.Type = KMTQAITYPE_GETSEGMENTSIZE; qa.pPrivateDriverData = &seg; qa.PrivateDriverDataSize = sizeof(seg);
    check(D3DKMTQueryAdapterInfo(&qa) == 0 && seg.DedicatedVideoMemorySize == 0 && seg.SharedSystemMemorySize == (256u << 20),
          "its memory is the driver's one segment: 256 MiB of system memory");
    D3DKMT_QUERYVIDEOMEMORYINFO vm;
    memset(&vm, 0, sizeof(vm));
    vm.hAdapter = adapter;
    check(D3DKMTQueryVideoMemoryInfo(&vm) == 0 && vm.Budget == (256u << 20) && vm.CurrentUsage == 0,
          "its budget is the segment, nothing used");

    D3DKMT_CREATEDEVICE cd;
    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = adapter;
    check(D3DKMTCreateDevice(&cd) == 0 && cd.hDevice, "CreateDevice (DxgkDdiCreateDevice)");
    D3DKMT_HANDLE dev = cd.hDevice;

    D3DKMT_HANDLE a1 = allocate(dev, 4097, &st);
    check(a1 != 0, "CreateAllocation2 (DxgkDdiCreateAllocation, DxgkDdiOpenAllocation)");
    check(usage(adapter) == 65536, "the driver sized it: one 64 KiB GPU page");
    D3DKMT_HANDLE a2 = allocate(dev, 3u << 20, &st);
    check(a2 != 0 && a2 != a1 && usage(adapter) == 65536 + (3u << 20), "a second one, 3 MiB");
    allocate(dev, 512u << 20, &st);
    check(st != 0 && usage(adapter) == 65536 + (3u << 20), "one bigger than the segment is refused");
    check(usage(basic) == 0, "Basic Render's memory is not touched");

    D3DKMT_LOCK2 lk = { dev, a2, 0, NULL };
    check(D3DKMTLock2(&lk) == 0 && lk.pData, "Lock2 maps it");
    unsigned char *p = lk.pData;
    if (p) { memset(p, 0x3C, 3u << 20); }
    D3DKMT_UNLOCK2 ul = { dev, a2 };
    check(D3DKMTUnlock2(&ul) == 0, "Unlock2");
    lk.pData = NULL;
    check(D3DKMTLock2(&lk) == 0 && lk.pData && ((unsigned char *)lk.pData)[(3u << 20) - 1] == 0x3C,
          "a second Lock2 sees what was written");
    D3DKMTUnlock2(&ul);
    D3DKMT_DESTROYALLOCATION2 da = { dev, 0, &a2, 1, 0 };
    check(D3DKMTDestroyAllocation2(&da) == 0 && usage(adapter) == 65536,
          "DestroyAllocation2 gives it back (DxgkDdiCloseAllocation, DxgkDdiDestroyAllocation)");
    gpu_va(adapter, dev);
    check(usage(adapter) == 65536, "the mapped allocation's memory is back");
    submission(dev);
    check(usage(adapter) == 65536, "the buffers' memory is back");
    tdr(dev);
    check(usage(adapter) == 65536, "and after the preemption and TDR checks");
    D3DKMT_DESTROYDEVICE dd = { dev };
    check(D3DKMTDestroyDevice(&dd) == 0 && usage(adapter) == 0, "DestroyDevice takes the other with it");
    D3DKMT_CLOSEADAPTER close = { adapter };
    check(D3DKMTCloseAdapter(&close) == 0, "CloseAdapter");
    printf("kmttest wddm: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}

int main(int argc, char **argv)
{
    LONG st;
    if (argc > 1 && !strcmp(argv[1], "create")) return create();
    if (argc > 1 && !strcmp(argv[1], "wddm")) return wddm();

    /* ---- adapters ---- */
    D3DKMT_ENUMADAPTERS2 en = { 0, NULL };
    check(D3DKMTEnumAdapters2(&en) == 0 && en.NumAdapters >= 1, "EnumAdapters2 counts the adapters");
    D3DKMT_ADAPTERINFO info[4];
    en.NumAdapters = 4;
    en.pAdapters = info;
    st = D3DKMTEnumAdapters2(&en);
    check(st == 0 && en.NumAdapters >= 1 && info[0].hAdapter, "EnumAdapters2 lists and opens them");
    if (st || !en.NumAdapters) { printf("kmttest: no adapter (%08lx)\n", (unsigned long)st); return 1; }
    UINT pick = en.NumAdapters - 1;              /* (Basic Render: listed last, after any hardware) */
    D3DKMT_HANDLE adapter = info[pick].hAdapter;
    LUID luid = info[pick].AdapterLuid;
    printf("kmttest: adapter LUID %08lx:%08lx\n", (unsigned long)luid.HighPart, (unsigned long)luid.LowPart);

    D3DKMT_ADAPTERTYPE type = { 0 };
    D3DKMT_QUERYADAPTERINFO qa = { adapter, KMTQAITYPE_ADAPTERTYPE, &type, sizeof(type) };
    check(D3DKMTQueryAdapterInfo(&qa) == 0 && (type.Value & D3DKMT_ADAPTERTYPE_RENDER_SUPPORTED) &&
          (type.Value & D3DKMT_ADAPTERTYPE_SOFTWARE_DEVICE) && !(type.Value & D3DKMT_ADAPTERTYPE_DISPLAY_SUPPORTED),
          "the adapter is a render-only software one (Basic Render)");
    D3DKMT_DRIVERVERSION ver = 0;
    qa.Type = KMTQAITYPE_DRIVERVERSION; qa.pPrivateDriverData = &ver; qa.PrivateDriverDataSize = sizeof(ver);
    check(D3DKMTQueryAdapterInfo(&qa) == 0 && ver >= KMT_DRIVERVERSION_WDDM_2_0, "its driver is WDDM 2.0 or later");
    D3DKMT_SEGMENTSIZEINFO seg;
    memset(&seg, 0, sizeof(seg));
    qa.Type = KMTQAITYPE_GETSEGMENTSIZE; qa.pPrivateDriverData = &seg; qa.PrivateDriverDataSize = sizeof(seg);
    check(D3DKMTQueryAdapterInfo(&qa) == 0 && seg.DedicatedVideoMemorySize == 0 && seg.SharedSystemMemorySize >= (16u << 20),
          "its memory is shared system memory, no VRAM");
    printf("kmttest: %llu MB of shared memory\n", (unsigned long long)(seg.SharedSystemMemorySize >> 20));
    qa.PrivateDriverDataSize = 4;
    check(D3DKMTQueryAdapterInfo(&qa) == ST_INVALID_PARAMETER, "a query with the wrong size is refused");

    D3DKMT_OPENADAPTERFROMLUID ol = { luid, 0 };
    check(D3DKMTOpenAdapterFromLuid(&ol) == 0 && ol.hAdapter && ol.hAdapter != adapter, "OpenAdapterFromLuid finds it");
    D3DKMT_CLOSEADAPTER close = { ol.hAdapter };
    check(D3DKMTCloseAdapter(&close) == 0, "CloseAdapter");
    check(D3DKMTCloseAdapter(&close) == ST_INVALID_PARAMETER, "a closed adapter handle is refused");
    D3DKMT_OPENADAPTERFROMLUID bad = { { luid.LowPart + 1, luid.HighPart }, 0 };
    check(D3DKMTOpenAdapterFromLuid(&bad) != 0, "an unknown LUID is refused");

    D3DKMT_CREATEDEVICE cd;
    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = adapter;
    check(D3DKMTCreateDevice(&cd) == 0 && cd.hDevice, "CreateDevice");
    g_dev = cd.hDevice;

    /* ---- allocations ---- */
    UINT64 before = usage(adapter);
    D3DKMT_HANDLE a1 = allocate(g_dev, 65536, &st);
    check(a1 != 0, "CreateAllocation2, 64 KiB");
    D3DKMT_HANDLE a2 = allocate(g_dev, (1u << 20) + 1, &st);
    check(a2 != 0 && a2 != a1, "CreateAllocation2, 1 MiB and a byte");
    check(usage(adapter) == before + 65536 + (1u << 20) + 4096, "the video memory counts them (whole pages)");
    allocate(g_dev, 0x80000000ull, &st);
    check(st != 0, "an allocation past the largest is refused");

    D3DKMT_LOCK2 lk = { g_dev, a2, 0, NULL };
    check(D3DKMTLock2(&lk) == 0 && lk.pData, "Lock2 maps it");
    unsigned char *p = lk.pData;
    MEMORY_BASIC_INFORMATION mbi;
    check(p && VirtualQuery(p, &mbi, sizeof(mbi)) && mbi.Protect == PAGE_READWRITE && mbi.RegionSize >= (1u << 20),
          "the mapping is read-write and covers it");
    int zero = 1;
    for (int i = 0; p && i < (1 << 20); i += 4096) zero &= p[i] == 0;
    check(zero, "a new allocation is zeroed");
    if (p) { memset(p, 0x5A, 1u << 20); p[(1u << 20)] = 0xA5; }
    D3DKMT_LOCK2 lk2 = { g_dev, a2, 0, NULL };
    check(D3DKMTLock2(&lk2) == 0 && lk2.pData == lk.pData, "a second lock gets the same mapping");
    D3DKMT_UNLOCK2 ul = { g_dev, a2 };
    check(D3DKMTUnlock2(&ul) == 0 && D3DKMTUnlock2(&ul) == 0, "Unlock2, twice");
    check(D3DKMTUnlock2(&ul) == ST_INVALID_PARAMETER, "an unlocked allocation cannot be unlocked");
    lk.pData = NULL;
    check(D3DKMTLock2(&lk) == 0 && lk.pData, "Lock2 again");
    p = lk.pData;
    check(p && p[0] == 0x5A && p[(1u << 20) - 1] == 0x5A && p[1u << 20] == 0xA5, "it kept what was written");
    D3DKMT_DESTROYALLOCATION2 da = { g_dev, 0, &a2, 1, 0 };
    check(D3DKMTDestroyAllocation2(&da) == 0, "DestroyAllocation2 (while locked)");
    check(!p || !VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State == MEM_FREE, "its mapping went with it");
    check(D3DKMTDestroyAllocation2(&da) == ST_INVALID_PARAMETER, "a destroyed allocation's handle is refused");
    check(usage(adapter) == before + 65536, "the video memory gives it back");

    /* ---- fences ---- */
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 cs;
    memset(&cs, 0, sizeof(cs));
    cs.hDevice = g_dev;
    cs.Info.Type = D3DDDI_MONITORED_FENCE;
    cs.Info.MonitoredFence.InitialFenceValue = 5;
    check(D3DKMTCreateSynchronizationObject2(&cs) == 0 && cs.hSyncObject, "CreateSynchronizationObject2, a monitored fence");
    g_fence = cs.hSyncObject;
    volatile UINT64 *cpu = cs.Info.MonitoredFence.FenceValueCPUVirtualAddress;
    check(cpu && *cpu == 5, "its value is at FenceValueCPUVirtualAddress");
    check(cpu && VirtualQuery((void *)cpu, &mbi, sizeof(mbi)) && mbi.Protect == PAGE_READONLY, "read-only there");

    UINT64 v = 10;
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU sg = { g_dev, 1, &g_fence, &v, 0 };
    check(D3DKMTSignalSynchronizationObjectFromCpu(&sg) == 0 && cpu && *cpu == 10, "SignalFromCpu raises it");
    v = 3;
    check(D3DKMTSignalSynchronizationObjectFromCpu(&sg) == 0 && cpu && *cpu == 10, "a lower value leaves it");
    v = 10;
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wt = { g_dev, 1, &g_fence, &v, NULL, 0 };
    check(D3DKMTWaitForSynchronizationObjectFromCpu(&wt) == 0, "a wait for a reached value returns");

    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    v = 20;
    wt.hAsyncEvent = ev;
    check(D3DKMTWaitForSynchronizationObjectFromCpu(&wt) == 0, "a wait with an event returns at once");
    check(WaitForSingleObject(ev, 50) == WAIT_TIMEOUT, "its event waits for the value");
    v = 20;
    check(D3DKMTSignalSynchronizationObjectFromCpu(&sg) == 0 && WaitForSingleObject(ev, 1000) == WAIT_OBJECT_0,
          "signalling the value sets the event");
    CloseHandle(ev);

    HANDLE th = CreateThread(NULL, 0, waiter, (void *)(ULONG_PTR)30, 0, NULL);
    check(WaitForSingleObject(th, 100) == WAIT_TIMEOUT, "another thread waits for 30");
    v = 30;
    D3DKMTSignalSynchronizationObjectFromCpu(&sg);
    DWORD code = 1;
    check(WaitForSingleObject(th, 5000) == WAIT_OBJECT_0 && GetExitCodeThread(th, &code) && code == 0,
          "and goes on when it is signalled");
    CloseHandle(th);

    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 cs2;
    memset(&cs2, 0, sizeof(cs2));
    cs2.hDevice = g_dev;
    cs2.Info.Type = D3DDDI_FENCE;
    cs2.Info.Fence.FenceValue = 1;
    check(D3DKMTCreateSynchronizationObject2(&cs2) == 0 && cs2.hSyncObject, "CreateSynchronizationObject2, a fence");
    D3DKMT_HANDLE two[2] = { g_fence, cs2.hSyncObject };
    UINT64 vals[2] = { 30, 2 };
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU any = { g_dev, 2, two, vals, NULL, 1 };
    check(D3DKMTWaitForSynchronizationObjectFromCpu(&any) == 0, "WaitAny returns when one is reached");
    ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU all = { g_dev, 2, two, vals, ev, 0 };
    check(D3DKMTWaitForSynchronizationObjectFromCpu(&all) == 0 && WaitForSingleObject(ev, 50) == WAIT_TIMEOUT,
          "waiting for all of them waits for the other");
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU sg2 = { g_dev, 1, &two[1], &vals[1], 0 };
    check(D3DKMTSignalSynchronizationObjectFromCpu(&sg2) == 0 && WaitForSingleObject(ev, 1000) == WAIT_OBJECT_0,
          "until it is reached too");
    CloseHandle(ev);

    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 mx;
    memset(&mx, 0, sizeof(mx));
    mx.hDevice = g_dev;
    mx.Info.Type = D3DDDI_SYNCHRONIZATION_MUTEX;
    check(D3DKMTCreateSynchronizationObject2(&mx) != 0, "the kinds not done yet are refused");
    D3DKMT_HANDLE junk = 0x12345;
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU sj = { g_dev, 1, &junk, &v, 0 };
    check(D3DKMTSignalSynchronizationObjectFromCpu(&sj) == ST_INVALID_PARAMETER, "a bad fence handle is refused");

    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT ds = { cs2.hSyncObject };
    check(D3DKMTDestroySynchronizationObject(&ds) == 0, "DestroySynchronizationObject");
    D3DKMT_DESTROYDEVICE dd = { g_dev };
    check(D3DKMTDestroyDevice(&dd) == 0, "DestroyDevice");
    check(cpu && (!VirtualQuery((void *)cpu, &mbi, sizeof(mbi)) || mbi.State == MEM_FREE),
          "the device's monitored fence went with it");
    check(D3DKMTSignalSynchronizationObjectFromCpu(&sg) == ST_INVALID_PARAMETER, "and so did its handles");
    check(usage(adapter) == before, "and its allocations");
    close.hAdapter = adapter;
    check(D3DKMTCloseAdapter(&close) == 0, "CloseAdapter (the listed one)");

    printf("kmttest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
