/* NovaOS: MinGW-w64's pathcch.h (which has no include guard and predates
 * PATHCCH_OPTIONS: its PATHCCH_ALLOW_LONG_PATHS is a plain number), plus the options WIL's filesystem helpers name */
#pragma once
#include_next <pathcch.h>

#ifndef PATHCCH_NONE
#undef PATHCCH_ALLOW_LONG_PATHS
typedef enum PATHCCH_OPTIONS
{
    PATHCCH_NONE = 0x0,
    PATHCCH_ALLOW_LONG_PATHS = 0x1,
    PATHCCH_FORCE_ENABLE_LONG_NAME_PROCESS = 0x2,
    PATHCCH_FORCE_DISABLE_LONG_NAME_PROCESS = 0x4,
    PATHCCH_DO_NOT_NORMALIZE_SEGMENTS = 0x8,
    PATHCCH_ENSURE_IS_EXTENDED_LENGTH_PATH = 0x10,
    PATHCCH_ENSURE_TRAILING_SLASH = 0x20,
    PATHCCH_CANONICALIZE_SLASHES = 0x40,
} PATHCCH_OPTIONS;
#ifdef __cplusplus
DEFINE_ENUM_FLAG_OPERATORS(PATHCCH_OPTIONS)
#endif
#endif
