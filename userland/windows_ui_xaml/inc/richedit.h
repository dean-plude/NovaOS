/* NovaOS: the Windows SDK's richedit.h for Microsoft's XAML sources:
 * MinGW-w64's, with the context menu request of Rich Edit 8 as the SDK has
 * it (GETCONTEXTMENUEX with the spelling suggestions and the autocorrected
 * word XAML's text boxes put on their context menu; MinGW-w64 stops at
 * pvReserved) and the auto URL flag MinGW-w64 leaves out. */
#pragma once
#define _getcontextmenuex nova_mingw_getcontextmenuex
#define GETCONTEXTMENUEX nova_mingw_GETCONTEXTMENUEX
#include_next <richedit.h>
#undef _getcontextmenuex
#undef GETCONTEXTMENUEX

struct IEnumString;
typedef struct _getcontextmenuex {
    CHARRANGE chrg;
    DWORD dwFlags;
    POINT pt;
    void *pvReserved;
    struct IEnumString *pSpellingSuggestions;
    WCHAR *bstrAutoCorrectedWord;               /* (a BSTR) */
} GETCONTEXTMENUEX;

#ifndef AURL_DISABLEAUTOFORMAT
#define AURL_DISABLEAUTOFORMAT 64
#endif
