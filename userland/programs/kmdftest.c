/* kmdftest.exe — talking to a KMDF driver: kmdftest.sys
 * (tools/drivertest/kmdftest.c), which runs on Microsoft's framework
 * (Wdf01000.sys, bound through WdfLdr.sys) and which the wdm self-test
 * loads with "sc create KmdfTest type= kernel" and "sc start KmdfTest"
 * first.  Opens \\.\KmdfTest (the control device's link: the driver's
 * EvtDeviceFileCreate), writes bytes it keeps and reads them back (its
 * default queue's EvtIoWrite and EvtIoRead), reverses bytes through a
 * buffered control code (EvtIoDeviceControl), sends one the driver does
 * not know, and asks what the driver saw of the framework: DriverEntry's
 * WdfDriverCreate, KMDF 1.33 available, the control device and its queue,
 * its typed context, PASSIVE_LEVEL callbacks and its registry path.
 *
 * "kmdftest absent" checks the device is gone (after "sc stop KmdfTest"). */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../../tools/drivertest/kmdftest.h"

/* (kernel32's; NovaOS's windows.h does not declare it) */
WINBASEAPI BOOL WINAPI DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD in_n, LPVOID out, DWORD out_n,
                                       LPDWORD returned, LPOVERLAPPED ov);

static int passed, failed;

static void check(int ok, const char *what)
{
    if (ok) passed++;
    else { failed++; printf("FAIL: %s (error %lu)\n", what, GetLastError()); }
}

static int wide_is(const unsigned short *w, const char *s)
{
    for (; *s; s++, w++) if ((*w | 0x20) != (*s | 0x20)) return 0;
    return !*w;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "absent")) {
        HANDLE h = CreateFileA("\\\\.\\KmdfTest", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        check(h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND, "\\\\.\\KmdfTest gone after sc stop");
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        printf("kmdftest absent: %d passed, %d failed\n", passed, failed);
        return failed;
    }

    HANDLE h = CreateFileA("\\\\.\\KmdfTest", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    check(h != INVALID_HANDLE_VALUE, "open \\\\.\\KmdfTest");
    if (h == INVALID_HANDLE_VALUE) {
        printf("kmdftest: %d passed, %d failed\n", passed, failed);
        return 1;
    }

    /* EvtIoWrite, EvtIoRead */
    char rbuf[64] = "";
    DWORD n = 0;
    const char msg[] = "hello from user mode";
    check(WriteFile(h, msg, sizeof(msg), &n, NULL) && n == sizeof(msg), "WriteFile");
    n = 0;
    check(ReadFile(h, rbuf, sizeof(rbuf), &n, NULL) && n == sizeof(msg) && !memcmp(rbuf, msg, sizeof(msg)),
          "ReadFile returns what was written");
    printf("  read back: \"%s\" (%lu bytes)\n", rbuf, n);

    /* EvtIoDeviceControl */
    char in[] = "abcdef", out[16] = "";
    n = 0;
    check(DeviceIoControl(h, IOCTL_KMDFTEST_REVERSE, in, 6, out, sizeof(out), &n, NULL) && n == 6 &&
          !memcmp(out, "fedcba", 6), "IOCTL_KMDFTEST_REVERSE");
    check(!DeviceIoControl(h, IOCTL_KMDFTEST_REVERSE, in, 6, out, 2, &n, NULL), "too small an output buffer fails");
    check(!DeviceIoControl(h, KMDFTEST_CTL(0x8FF, 0), NULL, 0, NULL, 0, &n, NULL) &&
          GetLastError() == ERROR_INVALID_FUNCTION, "an unknown control code: ERROR_INVALID_FUNCTION");

    KMDFTEST_INFO info;
    memset(&info, 0, sizeof(info));
    n = 0;
    check(DeviceIoControl(h, IOCTL_KMDFTEST_INFO, NULL, 0, &info, sizeof(info), &n, NULL) && n == sizeof(info),
          "IOCTL_KMDFTEST_INFO");
    check(info.driver_entry, "DriverEntry ran WdfDriverCreate");
    check(info.major == 1 && info.minor == 33, "WdfDriverIsVersionAvailable: KMDF 1.33");
    check(info.device_created, "WdfDeviceCreate, WdfDeviceCreateSymbolicLink and WdfIoQueueCreate");
    check(info.context, "the device's typed context");
    check(info.passive, "EvtIoDeviceControl at PASSIVE_LEVEL");
    check(info.creates == 1, "one EvtDeviceFileCreate");
    check(info.writes == 1, "one EvtIoWrite");
    check(wide_is(info.registry_path, "\\Registry\\Machine\\System\\CurrentControlSet\\Services\\KmdfTest"),
          "WdfDriverGetRegistryPath");
    printf("  KMDF %u.%u, creates %u, writes %u\n", info.major, info.minor, info.creates, info.writes);
    CloseHandle(h);

    printf("kmdftest: %d passed, %d failed\n", passed, failed);
    return failed;
}
