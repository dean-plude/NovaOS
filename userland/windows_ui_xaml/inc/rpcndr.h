/* NovaOS: MinGW-w64's rpcndr.h, with MIDL_INTERFACE giving the interface
 * its IID as the Windows SDK's does (MinGW-w64 keeps IIDs in templates of
 * its own when the compiler is not MSVC; clang in MSVC mode takes
 * __uuidof from __declspec(uuid)) */
#pragma once
#include_next <rpcndr.h>
#undef MIDL_INTERFACE
#define MIDL_INTERFACE(x) struct __declspec(uuid(x)) __declspec(novtable)
