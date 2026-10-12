/* umdftest.exe — a program using a UMDF 2 driver's device: umdftest.dll
 * (tools/drivertest/umdftest.c), a user-mode driver for QEMU's edu device
 * the umdf self-test installs with "pnputil /add-driver umdftest.inf
 * /install".  The driver runs in a driver host process (WUDFHost.exe);
 * the program's requests reach it through the reflector, WUDFRd.sys.
 * Finds the device through the interface the driver registered
 * (CM_Get_Device_Interface_List), opens it, writes bytes and reads them
 * back, has the driver reverse a buffer (a device control), and asks it
 * what it saw: the framework version, the host process (not this one),
 * the requesting process (this one), the device's resources and its
 * registry path. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../../tools/drivertest/umdftest.h"

WINBASEAPI BOOL WINAPI DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD in_n, LPVOID out, DWORD out_n,
                                       LPDWORD returned, LPOVERLAPPED ov);
typedef DWORD CONFIGRET;
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeW(PULONG len, LPGUID cls, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_Interface_ListW(LPGUID cls, LPCWSTR id, WCHAR *buf, ULONG len, ULONG flags);

static int passed, failed;

static void check(int ok, const char *what)
{
    if (ok) passed++;
    else { failed++; printf("FAIL: %s (error %lu)\n", what, GetLastError()); }
}

static int done(void)
{
    printf("umdftest: %d passed, %d failed\n", passed, failed);
    return failed;
}

int main(void)
{
    GUID g = UMDFTEST_GUID_INIT;
    WCHAR path[512];
    ULONG len = 0;
    path[0] = 0;
    /* (the host starts after the device: give it a few seconds to register the interface) */
    for (int i = 0; i < 50 && !path[0]; i++) {
        if (!CM_Get_Device_Interface_List_SizeW(&len, &g, NULL, 0) && len <= 512 &&
            CM_Get_Device_Interface_ListW(&g, NULL, path, 512, 0))
            path[0] = 0;
        if (!path[0]) Sleep(200);
    }
    check(path[0] != 0, "the UMDF device's interface is listed");
    if (!path[0]) return done();
    printf("interface: %ls\n", path);
    HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    check(h != INVALID_HANDLE_VALUE, "open the interface");
    if (h == INVALID_HANDLE_VALUE) return done();

    static const char msg[] = "hello from a user-mode driver";
    char back[64];
    DWORD n = 0;
    check(WriteFile(h, msg, sizeof(msg), &n, NULL) && n == sizeof(msg), "write to the driver");
    memset(back, 0, sizeof(back));
    check(ReadFile(h, back, sizeof(back), &n, NULL) && n == sizeof(msg) && !memcmp(back, msg, sizeof(msg)),
          "read the bytes back");
    printf("read back: %u bytes '%s'\n", (unsigned)n, back);

    char rev[8] = "abcdef", out[8];
    memset(out, 0, sizeof(out));
    check(DeviceIoControl(h, IOCTL_UMDFTEST_REVERSE, rev, 6, out, sizeof(out), &n, NULL) && n == 6 &&
          !memcmp(out, "fedcba", 6), "IOCTL_UMDFTEST_REVERSE");
    printf("reversed: '%s'\n", out);
    check(!DeviceIoControl(h, UMDFTEST_CTL(0x9ff), NULL, 0, NULL, 0, &n, NULL), "an unknown control code fails");

    UMDFTEST_INFO info;
    memset(&info, 0, sizeof(info));
    check(DeviceIoControl(h, IOCTL_UMDFTEST_INFO, NULL, 0, &info, sizeof(info), &n, NULL) && n == sizeof(info),
          "IOCTL_UMDFTEST_INFO");
    printf("UMDF %u.%u, driver host %u, requests from %u, EvtDriverDeviceAdd %u, EvtDevicePrepareHardware %u "
           "(%u resources: %u memory range(s) of %u bytes, %u interrupt(s)), EvtDeviceD0Entry %u, %u create(s)\n",
           info.major, info.minor, info.host_pid, info.requestor_pid, info.device_adds, info.prepare_hardware,
           info.resources_raw, info.memory, info.memory_length, info.interrupts, info.d0_entries, info.creates);
    printf("registry path: %ls\n", (const WCHAR *)info.registry_path);
    check(info.major == 2 && info.minor == 33, "the framework is UMDF 2.33");
    check(info.host_pid != 0 && info.host_pid != GetCurrentProcessId(), "the driver runs in its own host process");
    check(info.requestor_pid == GetCurrentProcessId(), "the driver sees this process as the requestor");
    check(info.device_adds == 1 && info.prepare_hardware >= 1 && info.d0_entries >= 1,
          "EvtDriverDeviceAdd, EvtDevicePrepareHardware and EvtDeviceD0Entry ran");
    check(info.memory == 1 && info.memory_length == 0x100000 && info.interrupts == 1,
          "the edu device's 1 MiB BAR and interrupt in the resources");
    check(info.creates >= 1 && info.stored == sizeof(msg), "EvtDeviceFileCreate ran and the write was kept");
    check(info.context_ok, "the device's typed context");
    check(!_wcsicmp((const WCHAR *)info.registry_path, L"SYSTEM\\CurrentControlSet\\Services\\WUDF\\Services\\UmdfTest") ||
          !_wcsicmp((const WCHAR *)info.registry_path,
                    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\WUDF\\Services\\UmdfTest"),
          "the driver's registry path is its UMDF service key");
    CloseHandle(h);
    return done();
}
