/*
 * uuid.c — COM's IID_IUnknown, from uuid.lib on Windows, for the framework
 * and the driver host
 */
#include <windows.h>

const GUID IID_IUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
