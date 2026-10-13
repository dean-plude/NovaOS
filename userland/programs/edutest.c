/* edutest.exe — a program using a Plug and Play driver's device: edutest.sys
 * (tools/drivertest/edutest.c), the WDM driver for QEMU's edu device, which
 * the edu self-test installs with "pnputil /add-driver edutest.inf /install",
 * or kmdfedu.sys (tools/drivertest/kmdfedu.c), its KMDF counterpart, which
 * the kmdfedu self-test installs (the driver says which it is).
 * Finds the device through the interface the driver registered
 * (CM_Get_Device_Interface_List, as Windows programs do), opens it, asks
 * the driver what IRP_MN_START_DEVICE gave it (the memory BAR, the
 * interrupt, the DMA adapter), has the device compute a factorial (its
 * interrupt reaches the driver's ISR and DPC) and move 4096 bytes and an
 * odd-sized buffer to the device and back by DMA (a scatter/gather list,
 * a common buffer, map registers).
 *
 *   edutest          the checks above
 *   edutest veto     with the device open, disabling it is refused (the
 *                    driver vetoes IRP_MN_QUERY_REMOVE_DEVICE; disabling
 *                    needs SeLoadDriverPrivilege: edutest.manifest asks
 *                    for highestAvailable)
 *   edutest absent   no device has the interface (after pnputil
 *                    /disable-device), and the old path no longer opens
 *   edutest message  as edutest, and the interrupt must have come as an MSI message
 *   edutest line     as edutest, and as the device's line (INTx)
 *   edutest msi 1|0  turn MSISupported (HKLM\...\Enum\<device>\Device Parameters\Interrupt
 *                    Management\MessageSignaledInterruptProperties) on or off; the device
 *                    takes it when it starts next
 *
 * Without "message" or "line" either is accepted: the driver's INF does not set
 * MSISupported, so the device starts on its line until "msi 1". */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../../tools/drivertest/edutest.h"

WINBASEAPI BOOL WINAPI DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD in_n, LPVOID out, DWORD out_n,
                                       LPDWORD returned, LPOVERLAPPED ov);
typedef DWORD CONFIGRET;
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeW(PULONG len, LPGUID cls, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_Interface_ListW(LPGUID cls, LPCWSTR id, WCHAR *buf, ULONG len, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Locate_DevNodeW(PDWORD dn, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Disable_DevNode(DWORD dn, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_ID_List_SizeW(PULONG len, LPCWSTR filter, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_ID_ListW(LPCWSTR filter, WCHAR *buf, ULONG len, ULONG flags);

static int passed, failed;

static void check(int ok, const char *what)
{
    if (ok) passed++;
    else { failed++; printf("FAIL: %s (error %lu)\n", what, GetLastError()); }
}

/* The first path with the driver's interface, or "" */
static void interface_path(WCHAR *out, int cap)
{
    GUID g = EDUTEST_GUID_INIT;
    ULONG len = 0;
    out[0] = 0;
    if (CM_Get_Device_Interface_List_SizeW(&len, &g, NULL, 0) || len > (ULONG)cap) return;
    if (CM_Get_Device_Interface_ListW(&g, NULL, out, (ULONG)cap, 0)) out[0] = 0;
}

/* The device instance ID in an interface path: \\?\PCI#VEN_...#...#{guid} */
static void instance_of(const WCHAR *path, WCHAR *out, int cap)
{
    const WCHAR *s = path + 4;
    int k = 0;
    const WCHAR *end = wcsrchr(s, L'#');
    for (; *s && s < end && k < cap - 1; s++) out[k++] = *s == L'#' ? L'\\' : *s;
    out[k] = 0;
}

/* The interface path the edu device had: from its instance ID */
static BOOL former_path(WCHAR *out, int cap)
{
    static WCHAR ids[16384];
    ULONG len = 0;
    if (CM_Get_Device_ID_List_SizeW(&len, NULL, 0) || len > 16384 || CM_Get_Device_ID_ListW(NULL, ids, 16384, 0)) return FALSE;
    for (WCHAR *id = ids; *id; id += wcslen(id) + 1) {
        if (_wcsnicmp(id, L"PCI\\VEN_1234&DEV_11E8", 21)) continue;
        int k = swprintf(out, cap, L"\\\\?\\");
        for (const WCHAR *c = id; *c && k < cap - 48; c++) out[k++] = *c == L'\\' ? L'#' : *c;
        swprintf(out + k, cap - k, L"#{6b1e0a1c-5f3d-4c8e-9a37-3e0d2b8c4f11}");
        return TRUE;
    }
    return FALSE;
}

static int done(const char *what)
{
    printf("%s: %d passed, %d failed\n", what, passed, failed);
    return failed;
}

/* MSISupported of the device whose interface path (or former path) is @path */
static BOOL set_msi(const WCHAR *path, DWORD on)
{
    WCHAR inst[256], key[512];
    instance_of(path, inst, 256);
    swprintf(key, 512, L"SYSTEM\\CurrentControlSet\\Enum\\%ls\\Device Parameters\\Interrupt Management\\"
                       L"MessageSignaledInterruptProperties", inst);
    HKEY k;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) return FALSE;
    LONG e = RegSetValueExW(k, L"MSISupported", 0, REG_DWORD, (const BYTE *)&on, sizeof(on));
    RegCloseKey(k);
    return !e;
}

int main(int argc, char **argv)
{
    WCHAR path[512];
    interface_path(path, 512);
    if (argc > 2 && !strcmp(argv[1], "msi")) {
        WCHAR old[512];
        const WCHAR *where = path[0] ? path : (former_path(old, 512) ? old : NULL);
        check(where != NULL, "the edu device is in the device tree");
        check(where && set_msi(where, (DWORD)atoi(argv[2])), "set MSISupported");
        printf("edu: MSISupported = %s\n", argv[2]);
        return done("edutest msi");
    }
    if (argc > 1 && !strcmp(argv[1], "absent")) {
        check(!path[0], "no device has edutest's interface after it was disabled");
        WCHAR old[512];
        check(former_path(old, 512), "the edu device is still in the device tree");
        HANDLE h = CreateFileW(old, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        check(h == INVALID_HANDLE_VALUE, "its interface path no longer opens");
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        return done("edutest absent");
    }
    check(path[0] != 0, "the edu device's interface is listed");
    if (!path[0]) return done("edutest");
    printf("interface: %ls\n", path);
    HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    check(h != INVALID_HANDLE_VALUE, "open the interface");
    if (h == INVALID_HANDLE_VALUE) return done("edutest");

    if (argc > 1 && !strcmp(argv[1], "veto")) {
        WCHAR inst[256];
        DWORD dn;
        instance_of(path, inst, 256);
        check(!CM_Locate_DevNodeW(&dn, inst, 0), "locate the device node");
        CONFIGRET cr = CM_Disable_DevNode(dn, 0);
        printf("disable while open: CONFIGRET 0x%lx\n", (unsigned long)cr);
        check(cr == 0x17, "disabling it while open is vetoed (CR_REMOVE_VETOED)");
        DWORD n = 0;
        EDUTEST_INFO info;
        check(DeviceIoControl(h, IOCTL_EDUTEST_INFO, NULL, 0, &info, sizeof(info), &n, NULL), "the driver still answers");
        CloseHandle(h);
        return done("edutest veto");
    }

    EDUTEST_INFO info;
    DWORD n = 0;
    memset(&info, 0, sizeof(info));
    check(DeviceIoControl(h, IOCTL_EDUTEST_INFO, NULL, 0, &info, sizeof(info), &n, NULL) && n == sizeof(info),
          "IOCTL_EDUTEST_INFO");
    printf("edu: ident %08x, BAR at %llx (%u bytes), interrupt vector %u at IRQL %u (GSI %u, %s), "
           "connected as version %u, DMA adapter v%u with %u map registers\n",
           info.ident, (unsigned long long)info.bar_raw, info.bar_length, info.vector, info.irql, info.raw_level,
           info.latched ? "edge" : "level", info.connect_version, info.dma_version, info.dma_registers);
    printf("location: %ls\n", (const WCHAR *)info.location);
    if (info.kmdf_minor) printf("edu: a KMDF 1.%u driver, its interrupt object %s\n", info.kmdf_minor,
                                info.wdf_message ? "message-signalled" : "on the line");
    check(info.ident == 0x010000ed, "edu's identification register");
    check(info.liveness == ~0x12345678u, "edu's liveness register inverts what was written");
    check(info.memory == 1 && info.interrupts == 1, "one memory BAR and one interrupt in the resources");
    check(info.bar_length == 0x100000, "the BAR is 1 MiB");
    check(info.vector >= 0x30 && info.vector < 0xF0 && info.irql == info.vector >> 4, "the translated interrupt's vector and IRQL");
    if (argc > 1) {
        check(!strcmp(argv[1], "message") ? info.message : !info.message, "the interrupt came in the mode asked for");
    }
    if (info.kmdf_minor) {
        /* (the framework connects the interrupt itself: what WdfInterruptGetInfo says of it) */
        check(info.wdf_message == info.message, "the interrupt object is message-signalled exactly when the resources say so");
        check(info.message_vector == info.vector && info.message_irql == info.irql,
              "WdfInterruptGetInfo agrees with the translated resources");
        if (info.message) {
            printf("edu: MSI: %u message(s) from vector %u (IRQL %u)\n", info.message_count, info.message_vector,
                   info.message_irql);
            check(info.message_count == 1, "edu's one message in the raw descriptor");
        } else {
            check(!info.latched, "a PCI INTx interrupt is level-triggered");
        }
    } else if (info.message) {
        printf("edu: MSI: %u message(s) from vector %u (IRQL %u, table says %u), address %llx data %x\n", info.message_count,
               info.message_vector, info.message_irql, info.irql, (unsigned long long)info.message_address, info.message_data);
        check(info.connect_version == 3, "IoConnectInterruptEx connected message-based");
        check(info.message_count == 1, "edu's one message, in the raw descriptor and the table");
        check(info.message_vector == info.vector && info.message_irql == info.irql, "the table agrees with the translated list");
        check((info.message_address & ~0xFF000ull) == 0xFEE00000ull && info.message_data == info.message_vector,
              "the message is a write to the local APIC for the vector");
    } else {
        check(!info.latched, "a PCI INTx interrupt is level-triggered");
        check(info.connect_version == 2, "IoConnectInterruptEx fell back to line-based (no MSI)");
    }
    check(info.dma_version == 3 && info.dma_registers > 0, "a version 3 DMA adapter with map registers");
    check(info.starts >= 1, "IRP_MN_START_DEVICE reached the driver");

    EDUTEST_FACT f;
    unsigned int arg = 10;
    memset(&f, 0, sizeof(f));
    unsigned int before = 0;
    for (int round = 0; round < 2; round++) {
        check(DeviceIoControl(h, IOCTL_EDUTEST_FACTORIAL, &arg, sizeof(arg), &f, sizeof(f), &n, NULL) && n == sizeof(f),
              "IOCTL_EDUTEST_FACTORIAL");
        if (!round) before = f.interrupts;
        arg = 12;
    }
    printf("factorial 12 = %u (interrupt status %x, ISR at IRQL %u, DPC at IRQL %u, %u interrupts)\n",
           f.value, f.isr_status, f.isr_irql, f.dpc_irql, f.interrupts);
    check(f.value == 479001600u, "the device computed 12!");
    check(f.isr_status == 1, "the ISR saw the factorial interrupt");
    check(f.isr_irql == info.irql, "the ISR ran at the interrupt's IRQL");
    check(f.dpc_irql == 2, "its DPC ran at DISPATCH_LEVEL");
    check(f.interrupts == before + 1, "one interrupt per factorial");
    check(info.message ? f.message_interrupts == f.interrupts : f.message_interrupts == 0,
          "the interrupts came through the routine the mode calls for");

    static unsigned char buf[sizeof(EDUTEST_DMA) + 4096], data[4096];
    static const unsigned int sizes[] = { 4096, 1500 };
    for (int i = 0; i < 2; i++) {
        unsigned int len = sizes[i];
        for (unsigned int k = 0; k < len; k++) data[k] = (unsigned char)(k * 7 + i * 13 + 1);
        memset(buf, 0, sizeof(buf));
        memcpy(buf, data, len);
        BOOL ok = DeviceIoControl(h, IOCTL_EDUTEST_DMA, buf, len, buf, sizeof(EDUTEST_DMA) + len, &n, NULL);
        check(ok && n == sizeof(EDUTEST_DMA) + len, "IOCTL_EDUTEST_DMA");
        EDUTEST_DMA *r = (EDUTEST_DMA *)buf;
        printf("dma %u bytes: %u scatter/gather element(s) from %llx, common buffer at %llx %s, map registers at %llx %s, "
               "%u DMA interrupts\n", len, r->sg_elements, (unsigned long long)r->sg_address,
               (unsigned long long)r->common_logical, r->common_ok ? "ok" : "WRONG", (unsigned long long)r->map_address,
               r->map_ok ? "ok" : "WRONG", r->interrupts);
        check(ok && r->sg_elements >= 1, "a scatter/gather list for the transfer to the device");
        check(ok && r->common_ok, "the bytes came back into the common buffer");
        check(ok && r->common_logical > 0 && r->common_logical < 0x10000000, "the common buffer is within edu's 28 bits");
        check(ok && r->map_ok && !memcmp(buf + sizeof(EDUTEST_DMA), data, len), "the bytes came back through map registers");
        check(ok && r->sg_address < 0x10000000 && r->map_address < 0x10000000, "every DMA address is within edu's 28 bits");
    }
    CloseHandle(h);
    return done("edutest");
}
