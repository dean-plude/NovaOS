/* novaos_umdf_client.h — what the WDK build environment defines for a
 * UMDF 2 driver (tools/drivertest/umdftest.c and the stub every driver
 * links, tools/umdf/stub.c): user mode, x64, Windows 10, UMDF 2.33 */
#pragma once
#define _AMD64_ 1
#define AMD64 1
#define _WIN64 1
#define NTDDI_VERSION 0x0A000000
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#define UNICODE 1
#define _UNICODE 1
#define UMDF_VERSION_MAJOR 2
#define UMDF_VERSION_MINOR 33
#include "novaos_sal.h"
#define __control_entrypoint(x)
#define CFORCEINLINE __forceinline
