/* serialtest.exe — a COM port through the communications functions
 *
 *   serialtest PORT [BYTES]   open PORT (COM3, \\.\COM3), set it to 115200
 *                             8N1 with no flow control and read timeouts,
 *                             write BYTES (4096) of a pattern and read them
 *                             back: the port's far end must loop its data
 *                             back (the usb-serial self-test's QEMU chardev
 *                             is a socket sending back what it is sent)
 *
 * Each check prints "ok" or "FAIL" and the last line counts them, as the
 * self-tests expect.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static int g_passed, g_failed;

static void check(int ok, const char *what)
{
    printf("%s: %s", what, ok ? "ok\n" : "FAIL");
    if (!ok) printf(" (error %lu)\n", (unsigned long)GetLastError());
    if (ok) g_passed++;
    else g_failed++;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: serialtest PORT [BYTES]\n");
        return 2;
    }
    const char *port = argv[1];
    int total = argc > 2 ? atoi(argv[2]) : 4096;
    if (total <= 0 || total > 1 << 20) total = 4096;

    HANDLE h = CreateFileA(port, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    check(h != INVALID_HANDLE_VALUE, "open");
    if (h == INVALID_HANDLE_VALUE) {
        printf("serialtest: %d passed, %d failed\n", g_passed, g_failed);
        return 1;
    }

    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    check(GetCommState(h, &dcb), "GetCommState");
    printf("port was %lu baud, %u data bits, parity %u, stop bits %u\n", (unsigned long)dcb.BaudRate, dcb.ByteSize,
           dcb.Parity, dcb.StopBits);
    check(BuildCommDCBA("baud=115200 parity=n data=8 stop=1 octs=off odsr=off dtr=on rts=on", &dcb), "BuildCommDCB");
    dcb.fBinary = 1;
    dcb.fOutX = dcb.fInX = 0;
    check(SetCommState(h, &dcb), "SetCommState");
    DCB now;
    memset(&now, 0, sizeof(now));
    now.DCBlength = sizeof(now);
    check(GetCommState(h, &now) && now.BaudRate == 115200 && now.ByteSize == 8 && now.Parity == NOPARITY &&
              now.StopBits == ONESTOPBIT,
          "115200 8N1 set");

    COMMTIMEOUTS t = { 0 };
    t.ReadIntervalTimeout = 50;
    t.ReadTotalTimeoutConstant = 2000;
    t.WriteTotalTimeoutConstant = 5000;
    check(SetCommTimeouts(h, &t), "SetCommTimeouts");
    check(SetupComm(h, 8192, 8192), "SetupComm");
    check(PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR), "PurgeComm");
    DWORD ms = 0;
    check(GetCommModemStatus(h, &ms), "GetCommModemStatus");

    unsigned char *out = malloc((size_t)total), *in = malloc((size_t)total);
    for (int i = 0; i < total; i++) out[i] = (unsigned char)(i * 7 + (i >> 8));
    DWORD done = 0, got = 0;
    DWORD start = GetTickCount();
    BOOL wrote = WriteFile(h, out, (DWORD)total, &done, NULL) && done == (DWORD)total;
    check(wrote, "write");
    printf("wrote %lu of %d bytes\n", (unsigned long)done, total);
    for (int tries = 0; got < (DWORD)total && tries < 20; tries++) {
        DWORD n = 0;
        if (!ReadFile(h, in + got, (DWORD)total - got, &n, NULL)) break;
        got += n;
    }
    DWORD ms_taken = GetTickCount() - start;
    printf("read back %lu of %d bytes in %lu ms\n", (unsigned long)got, total, (unsigned long)ms_taken);
    check(got == (DWORD)total && !memcmp(in, out, (size_t)total), "loopback data matches");

    COMSTAT st;
    DWORD errors = 0;
    check(ClearCommError(h, &errors, &st), "ClearCommError");
    printf("errors 0x%lx, %lu queued in, %lu queued out\n", (unsigned long)errors, (unsigned long)st.cbInQue,
           (unsigned long)st.cbOutQue);
    check(CloseHandle(h), "close");
    free(out);
    free(in);
    printf("serialtest: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
