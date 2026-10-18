/*
 * watchdog.c — the firmware's WDAT watchdog, else the chipset's TCO timer
 * (see watchdog.h)
 */

#include "watchdog.h"
#include "wdat.h"
#include "tco.h"
#include "../ke/printf.h"

bool WatchdogStart(UINT32 seconds)
{
    return WdatStart(seconds) || TcoStart(seconds);
}

void WatchdogReload(void)
{
    WdatReload();
    TcoReload();
}

void WatchdogStop(void)
{
    WdatStop();
    TcoStop();
}

void WatchdogDescribe(char *out, int cap)
{
    WdatDescribe(out, cap);
    if (!out[0] && TcoPresent()) ksnprintf(out, (size_t)cap, "Intel ICH9 TCO timer");
}
