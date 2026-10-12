/* novaos_client.h — what the WDK build environment defines for a KMDF
 * client driver (tools/drivertest/kmdftest.c and the stub every client
 * links, tools/kmdf/stub.c): x64, Windows 10, KMDF 1.33 */
#pragma once
#define _AMD64_ 1
#define AMD64 1
#define _WIN64 1
#define NT_PROCESSOR_GROUPS 1
#define NTDDI_VERSION 0x0A000000
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#define KMDF_VERSION_MAJOR 1
#define KMDF_VERSION_MINOR 33
#include "novaos_sal.h"
