/*
 * shutdown.exe — shut down or restart the computer
 *
 *   shutdown /s | /p      shut down (power off)
 *   shutdown /r           restart
 *   shutdown /r /o        restart into Advanced startup (the boot loader's
 *                         menu: safe mode, startup repair, the previous version)
 *   shutdown /t N /f /c "comment" /d ...   accepted; NovaOS acts at once
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static void usage(void)
{
    printf("Usage: shutdown [/s | /p | /r [/o]] [/f] [/t xxx] [/c \"comment\"]\n\n"
           "    /s    Shut down the computer.\n"
           "    /p    Turn off the computer at once.\n"
           "    /r    Restart the computer.\n"
           "    /o    Go to the advanced boot options menu and restart the computer.\n"
           "          Must be used with /r.\n"
           "    /f    Close running programs without warning.\n"
           "    /t    Time-out before shutting down (NovaOS doesn't wait).\n");
}

int main(int argc, char **argv)
{
    UINT what = 0;
    BOOL options = FALSE;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '/' && a[0] != '-') { usage(); return 1; }
        switch (a[1] | 0x20) {
        case 's': case 'p': what = EWX_SHUTDOWN | EWX_POWEROFF; break;
        case 'r':           what = EWX_REBOOT; break;
        case 'f':           break;
        case 'o':           options = TRUE; break;
        case 't': case 'c': case 'd': if (i + 1 < argc) i++; break;
        case 'a':
            fprintf(stderr, "Unable to abort the system shutdown because no shutdown was in progress.(1116)\n");
            return 1116;
        case '?': default:  usage(); return a[1] == '?' ? 0 : 1;
        }
    }
    if (!what) { usage(); return 0; }
    if (options) {
        DWORD err = what == EWX_REBOOT ? InitiateShutdownW(NULL, NULL, 0, SHUTDOWN_RESTART | SHUTDOWN_RESTART_BOOTOPTIONS |
                                                           SHUTDOWN_FORCE_OTHERS, 0)
                                       : ERROR_INVALID_PARAMETER;
        if (err == ERROR_INVALID_PARAMETER) {
            fprintf(stderr, "The /o option must be used with /r.(87)\n");
            return 87;
        }
        if (err) {
            fprintf(stderr, "shutdown: failed (error %lu)\n", err);
            return 1;
        }
        return 0;
    }
    if (!ExitWindowsEx(what | EWX_FORCE, 0)) {
        fprintf(stderr, "shutdown: failed (error %lu)\n", GetLastError());
        return 1;
    }
    return 0;
}
