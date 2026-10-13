/* novaos_umdf.h — what the WDK build environment defined for every UMDF 2
 * framework file (-include'd by tools/umdf/build_umdf.py): user mode,
 * x64, Windows 10, UMDF 2.33 (the version of the KMDF 1.33 source), and
 * Windows' headers with NTSTATUS codes (UMDF_USING_NTSTATUS) */
#pragma once
#define FX_CORE_KERNEL_MODE 0
#define FX_CORE_USER_MODE   1
#define FX_CORE_MODE        FX_CORE_USER_MODE
#define _AMD64_ 1
#define AMD64 1
#define _WIN64 1
#define NTDDI_VERSION 0x0A000000
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#define UNICODE 1
#define _UNICODE 1
#define WIN32_LEAN_AND_MEAN 1
#define UMDF_USING_NTSTATUS 1
#define STRSAFE_NO_DEPRECATE 1
#define __WUDF_MAJOR_VERSION 2
#define __WUDF_MINOR_VERSION 33
#define __WUDF_SERVICE_VERSION 0
#define __WDF_MAJOR_VERSION 2
#define __WDF_MINOR_VERSION 33
#define __WDF_BUILD_NUMBER 0
#define __WDF_MAJOR_VERSION_STRING 02
#define __WDF_MINOR_VERSION_STRING 033
#define WDF_EVERYTHING_ALWAYS_AVAILABLE 1
#define EVENT_TRACING 1
#define WDF_PRODUCTION_RELEASE 1
#define WDF_REQUEST_REUSE_MUST_COMPLETE 2
#include "novaos_sal.h"
#define __control_entrypoint(x)

/* Windows' headers with the NTSTATUS codes: windows.h without its own
 * STATUS_* subset, then ntstatus.h (what UMDF_USING_NTSTATUS asks for) */
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <objbase.h>
#include <devpropdef.h>
#include <initguid.h>
#include <devpkey.h>
#undef INITGUID
#include <guiddef.h>
/* (MSVC's form, which also takes a const parameter) */
#undef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) ((void)(P))
#ifndef CFORCEINLINE
#define CFORCEINLINE FORCEINLINE
#endif
