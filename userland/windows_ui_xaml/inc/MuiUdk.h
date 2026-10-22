/* NovaOS: the Windows App SDK's MuiUdk.h (FrameworkUdk, shipped closed), as
 * far as XAML's text core calls it: the font fallback language list of a
 * language (core/text/common/TextFormatting.cpp), whose failure the text
 * core handles by using the language itself (src/framework_udk.cpp), and
 * the propagation of the app's languages to the process's MUI languages
 * (plat/win/desktop/CommonResourceProvider.cpp). */
#pragma once
#include <windows.h>

HRESULT Mui_GetFontFallbackLanguageList(PCWSTR languages, size_t cchBuffer, PWSTR buffer, size_t *cchWritten);
HRESULT Mui_PropagateApplicationLanguages();
