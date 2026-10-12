/* viosertest.exe — a program talking to the host through virtio-win's
 * VirtIO serial driver, vioser.sys, which the vioser self-test installs
 * with "pnputil /add-driver vioser.inf /install" on QEMU's virtio-serial
 * port.  vioser (a KMDF driver) makes each port a child device of its
 * own (a raw PDO) with the GUID_VIOSERIAL_PORT interface and, once the
 * host has named the port, the link \DosDevices\<name>.  The program
 * checks that the port's interface is listed, opens the port as
 * \\.\Global\org.novaos.test.0 and exchanges bytes with the host's end,
 * tools/drivertest/serialpeer.py, which sends back each byte XORed with
 * 0x5A: a line, then 64 KiB in 4 KiB pieces.  The port's device is for
 * administrators only (vioser gives it that security descriptor):
 * viosertest.manifest asks for highestAvailable.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef DWORD CONFIGRET;
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeW(PULONG len, LPGUID cls, LPCWSTR id, ULONG flags);
__declspec(dllimport) CONFIGRET WINAPI CM_Get_Device_Interface_ListW(LPGUID cls, LPCWSTR id, WCHAR *buf, ULONG len, ULONG flags);

/* GUID_VIOSERIAL_PORT (virtio-win's vioserial/sys/public.h) */
static GUID g_port = { 0x6fde7521, 0x1b65, 0x48ae, { 0xb6, 0x28, 0x80, 0xbe, 0x62, 0x01, 0x60, 0x26 } };

static int passed, failed;

static void check(int ok, const char *what)
{
    if (ok) passed++;
    else { failed++; printf("FAIL: %s (error %lu)\n", what, GetLastError()); }
}

/* Write @n bytes, then read back as many: true if every byte came back
 * XORed with 0x5A */
static BOOL exchange(HANDLE h, const BYTE *out, DWORD n)
{
    static BYTE in[4096];
    DWORD done = 0, got = 0;
    if (n > sizeof(in) || !WriteFile(h, out, n, &done, NULL) || done != n) return FALSE;
    while (got < n) {
        DWORD r = 0;
        if (!ReadFile(h, in + got, n - got, &r, NULL)) return FALSE;
        got += r;
    }
    for (DWORD i = 0; i < n; i++)
        if (in[i] != (out[i] ^ 0x5A)) return FALSE;
    return TRUE;
}

int main(void)
{
    WCHAR list[1024];
    ULONG len = 0;
    list[0] = 0;
    if (!CM_Get_Device_Interface_List_SizeW(&len, &g_port, NULL, 0) && len <= 1024)
        CM_Get_Device_Interface_ListW(&g_port, NULL, list, 1024, 0);
    check(list[0] != 0, "the port's GUID_VIOSERIAL_PORT interface is listed");
    if (list[0]) printf("port interface: %ls\n", list);

    HANDLE h = CreateFileW(L"\\\\.\\Global\\org.novaos.test.0", GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, 0, NULL);
    check(h != INVALID_HANDLE_VALUE, "open \\\\.\\Global\\org.novaos.test.0");
    if (h == INVALID_HANDLE_VALUE) goto out;

    static const char line[] = "hello from NovaOS through vioser.sys\n";
    BOOL ok = exchange(h, (const BYTE *)line, sizeof(line) - 1);
    check(ok, "a line to the host and back");
    if (ok) printf("line: %u bytes to the host and back\n", (unsigned)(sizeof(line) - 1));

    static BYTE block[4096];
    DWORD total = 0;
    ok = TRUE;
    for (int k = 0; k < 16 && ok; k++) {
        for (DWORD i = 0; i < sizeof(block); i++) block[i] = (BYTE)(i * 7 + k);
        ok = exchange(h, block, sizeof(block));
        if (ok) total += sizeof(block);
    }
    check(ok, "64 KiB to the host and back");
    printf("bulk: %lu bytes to the host and back\n", (unsigned long)total);
    CloseHandle(h);

out:
    printf("viosertest: %d passed, %d failed\n", passed, failed);
    return failed;
}
