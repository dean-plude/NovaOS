/* NovaOS: MinGW-w64's windows.h, plus the few Windows SDK names MRT Core
 * (third_party/mrtcore) uses that MinGW-w64 leaves out */
#pragma once
#include_next <windows.h>

#ifndef InterlockedIncrementNoFence
#define InterlockedIncrementNoFence InterlockedIncrement
#define InterlockedDecrementNoFence InterlockedDecrement
#endif

/* the Windows SDK's minwindef.h min and max, which MRT Core calls */
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

/* winerror.h's MRT error MinGW-w64 leaves out */
#ifndef ERROR_MRM_UNSUPPORTED_FILE_TYPE_FOR_LOAD_UNLOAD_PRI_FILE
#define ERROR_MRM_UNSUPPORTED_FILE_TYPE_FOR_LOAD_UNLOAD_PRI_FILE 15142L
#endif

/* intsafe.h's UINT32 multiply (the Windows SDK's; MinGW-w64 has UIntMult) */
#include <intsafe.h>
#ifndef UInt32Mult
#define UInt32Mult UIntMult
#endif
