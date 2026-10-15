/*
 * NovaOS: the Windows SDK's error codes that MinGW-w64's <winerror.h>
 * lacks, over it.
 */
#pragma once
#include_next <winerror.h>
#ifndef E_BOUNDS
#define E_BOUNDS _HRESULT_TYPEDEF_(0x8000000BL)
#endif
