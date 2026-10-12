/* wdmtest.exe — talking to a Windows kernel-mode driver: wdmtest.sys
 * (tools/drivertest/wdmtest.c), which the wdm self-test loads with
 * "sc create WdmTest type= kernel" and "sc start WdmTest" first.  Opens
 * \\.\WdmTest (the driver's \DosDevices link), writes bytes it keeps and
 * reads them back (IRP_MJ_WRITE, IRP_MJ_READ), reverses bytes through a
 * buffered control code and adds one to each through a METHOD_NEITHER one
 * on its own buffers (IRP_MJ_DEVICE_CONTROL), asks the driver how it sees
 * the objects (the DRIVER_OBJECT, DEVICE_OBJECT, IRP and FILE_OBJECT
 * types and sizes, its names and RegistryPath), opens \\.\WdmTest\sub to
 * see the rest of the name reach the driver, and checks that a direct-I/O
 * control code fails (MDLs come with step 30.2).  Closing the handles is
 * IRP_MJ_CLEANUP and IRP_MJ_CLOSE.
 *
 * "wdmtest absent" checks the device is gone (after "sc stop WdmTest"). */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../../tools/drivertest/wdmtest.h"

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

static void show(const char *label, const unsigned short *w)
{
    printf("  %s: ", label);
    for (; *w; w++) putchar(*w < 0x80 ? (char)*w : '?');
    putchar('\n');
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "absent")) {
        HANDLE h = CreateFileA("\\\\.\\WdmTest", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        check(h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND, "\\\\.\\WdmTest gone after sc stop");
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        printf("wdmtest absent: %d passed, %d failed\n", passed, failed);
        return failed;
    }

    HANDLE h = CreateFileA("\\\\.\\WdmTest", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    check(h != INVALID_HANDLE_VALUE, "open \\\\.\\WdmTest");
    if (h == INVALID_HANDLE_VALUE) {
        printf("wdmtest: %d passed, %d failed\n", passed, failed);
        return 1;
    }

    /* IRP_MJ_WRITE, IRP_MJ_READ */
    char rbuf[64] = "";
    DWORD n = 0;
    n = 99;
    check(ReadFile(h, rbuf, sizeof(rbuf), &n, NULL) && n == 0, "read before any write: end of file (0 bytes)");
    const char msg[] = "hello from user mode";
    check(WriteFile(h, msg, sizeof(msg), &n, NULL) && n == sizeof(msg), "WriteFile");
    n = 0;
    check(ReadFile(h, rbuf, sizeof(rbuf), &n, NULL) && n == sizeof(msg) && !memcmp(rbuf, msg, sizeof(msg)),
          "ReadFile returns what was written");
    printf("  read back: \"%s\" (%lu bytes)\n", rbuf, n);
    n = 0;
    check(ReadFile(h, rbuf, 5, &n, NULL) && n == 5 && !memcmp(rbuf, "hello", 5), "short ReadFile");

    /* IRP_MJ_DEVICE_CONTROL, METHOD_BUFFERED */
    char in[] = "abcdef", out[16] = "";
    n = 0;
    check(DeviceIoControl(h, IOCTL_WDMTEST_REVERSE, in, 6, out, sizeof(out), &n, NULL) && n == 6 &&
          !memcmp(out, "fedcba", 6), "buffered control code (reverse)");
    check(!DeviceIoControl(h, IOCTL_WDMTEST_REVERSE, in, 6, out, 2, &n, NULL) &&
          GetLastError() == ERROR_INSUFFICIENT_BUFFER, "buffered control code, output too small");

    /* METHOD_NEITHER: the driver works on these buffers directly */
    unsigned char nin[4] = { 1, 2, 3, 250 }, nout[4] = { 0 };
    n = 0;
    check(DeviceIoControl(h, IOCTL_WDMTEST_NEITHER, nin, 4, nout, 4, &n, NULL) && n == 4 &&
          nout[0] == 2 && nout[1] == 3 && nout[2] == 4 && nout[3] == 251, "neither-I/O control code");

    /* METHOD_IN_DIRECT: not served yet */
    check(!DeviceIoControl(h, IOCTL_WDMTEST_DIRECT, in, 6, out, sizeof(out), &n, NULL), "direct-I/O control code fails");

    /* an unknown control code */
    check(!DeviceIoControl(h, WDMTEST_CTL(0x8FF, 0), NULL, 0, NULL, 0, &n, NULL) &&
          GetLastError() == ERROR_INVALID_FUNCTION, "unknown control code: ERROR_INVALID_FUNCTION");

    /* the objects as the driver sees them */
    WDMTEST_INFO info;
    memset(&info, 0, sizeof(info));
    n = 0;
    check(DeviceIoControl(h, IOCTL_WDMTEST_INFO, NULL, 0, &info, sizeof(info), &n, NULL) && n == sizeof(info),
          "info control code");
    printf("  DRIVER_OBJECT type %d size %#x, DEVICE_OBJECT type %d size %#x, IRP type %d stack %d/%d, "
           "FILE_OBJECT type %d size %#x, requestor mode %u, buffered %u, opens %u\n",
           info.driver_type, info.driver_size, info.device_type, info.device_size, info.irp_type,
           info.irp_current_location, info.irp_stack_count, info.file_type, info.file_size, info.requestor_mode,
           info.buffered, info.opens);
    show("DriverName", info.driver_name);
    show("RegistryPath", info.registry_path);
    show("ServiceKeyName", info.service_key);
    check(info.driver_type == 4 && info.driver_size == 0x150, "DRIVER_OBJECT type and size");
    check(info.device_type == 3 && (unsigned short)info.device_size > 0x150, "DEVICE_OBJECT type and size");
    check(info.irp_type == 6 && info.irp_stack_count == 1 && info.irp_current_location == 1, "IRP type and stack");
    check(info.file_type == 5 && info.file_size == 0xD8, "FILE_OBJECT type and size");
    check(info.requestor_mode == 1 && info.buffered, "UserMode, buffered");
    check(info.opens == 1, "one IRP_MJ_CREATE so far");
    check(wide_is(info.driver_name, "\\Driver\\WdmTest"), "DriverName");
    check(wide_is(info.registry_path, "\\Registry\\Machine\\System\\CurrentControlSet\\Services\\WdmTest"),
          "RegistryPath");
    check(wide_is(info.service_key, "WdmTest"), "ServiceKeyName");
    check(info.file_name[0] == 0, "FileName of \\\\.\\WdmTest is empty");

    /* the rest of the name reaches the driver as the FILE_OBJECT's FileName */
    HANDLE h2 = CreateFileA("\\\\.\\WdmTest\\sub", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    check(h2 != INVALID_HANDLE_VALUE, "open \\\\.\\WdmTest\\sub");
    if (h2 != INVALID_HANDLE_VALUE) {
        memset(&info, 0, sizeof(info));
        check(DeviceIoControl(h2, IOCTL_WDMTEST_INFO, NULL, 0, &info, sizeof(info), &n, NULL) &&
              wide_is(info.file_name, "\\sub") && info.opens == 2, "FileName \\sub, second open");
        CloseHandle(h2);
    }
    check(CloseHandle(h), "close");

    /* a name with no device behind it */
    HANDLE h3 = CreateFileA("\\\\.\\WdmTestNone", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    check(h3 == INVALID_HANDLE_VALUE, "\\\\.\\WdmTestNone does not exist");
    if (h3 != INVALID_HANDLE_VALUE) CloseHandle(h3);

    printf("wdmtest: %d passed, %d failed\n", passed, failed);
    return failed;
}
