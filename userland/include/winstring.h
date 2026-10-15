/* winstring.h — Windows Runtime strings (HSTRING), in ole32 (Windows: combase) */
#pragma once
#include <objbase.h>

_NOVA_BEGIN

#ifndef __HSTRING_DEFINED
#define __HSTRING_DEFINED
typedef struct HSTRING__ *HSTRING;
typedef struct HSTRING_HEADER { union { void *Reserved1; char Reserved2[sizeof(void *) == 8 ? 24 : 20]; } Reserved; } HSTRING_HEADER;
#endif

WINOLEAPI_(HRESULT) WindowsCreateString(const WCHAR *src, UINT32 len, HSTRING *out);
WINOLEAPI_(HRESULT) WindowsCreateStringReference(const WCHAR *src, UINT32 len, HSTRING_HEADER *hdr, HSTRING *out);
WINOLEAPI_(HRESULT) WindowsDeleteString(HSTRING s);
WINOLEAPI_(HRESULT) WindowsDuplicateString(HSTRING s, HSTRING *out);
WINOLEAPI_(const WCHAR *) WindowsGetStringRawBuffer(HSTRING s, UINT32 *len);
WINOLEAPI_(UINT32) WindowsGetStringLen(HSTRING s);
WINOLEAPI_(BOOL) WindowsIsStringEmpty(HSTRING s);
WINOLEAPI_(HRESULT) WindowsCompareStringOrdinal(HSTRING a, HSTRING b, INT32 *result);
WINOLEAPI_(HRESULT) WindowsConcatString(HSTRING a, HSTRING b, HSTRING *out);

_NOVA_END
