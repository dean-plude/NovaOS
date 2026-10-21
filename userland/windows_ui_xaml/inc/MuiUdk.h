/* NovaOS: the Windows App SDK's MuiUdk.h (FrameworkUdk, shipped closed), as
 * far as XAML's text core calls it: the font fallback language list of a
 * language (core/text/common/TextFormatting.cpp), whose failure the text
 * core handles by using the language itself (src/framework_udk.cpp). */
#pragma once
#include <windows.h>

HRESULT Mui_GetFontFallbackLanguageList(PCWSTR languages, size_t cchBuffer, PWSTR buffer, size_t *cchWritten);
