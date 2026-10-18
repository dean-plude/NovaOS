/* NovaOS: MinGW-w64's tchar.h refuses to follow strsafe.h (its strsafe.h
 * then names the deprecated string functions); the Windows SDK's does not,
 * and XAML's URI library includes them in that order.  Its strsafe.h is
 * already in, so the check is skipped. */
#pragma once
#ifdef _STRSAFE_H_INCLUDED_
#pragma push_macro("_STRSAFE_H_INCLUDED_")
#undef _STRSAFE_H_INCLUDED_
#include_next <tchar.h>
#pragma pop_macro("_STRSAFE_H_INCLUDED_")
#else
#include_next <tchar.h>
#endif
