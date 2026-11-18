/* NovaOS: fontinfo.h, the Windows App SDK's (closed) header for
 * IProvideFontInfo, the interface a windowless Rich Edit host answers
 * (QueryInterface on its ITextHost) to give the text services its fonts as
 * DirectWrite font faces: XAML's text boxes give their FontFamily's faces
 * (core/native/text/Controls/TextServicesHost.cpp, CTextBoxView).  The
 * methods in the order XAML's host declares them; the IID is NovaOS's, the
 * one NovaOS's WinUIEdit.dll (userland/textserv, NOVA_IID_IProvideFontInfo
 * in userland/include/novatextserv.h) asks for. */
#pragma once
#include <dwrite.h>

struct DECLSPEC_UUID("d81d4036-c20c-4297-8cf1-5835a6766189") IProvideFontInfo : public IUnknown {
    /* the default font's family name (a BSTR the caller frees) */
    virtual BSTR GetDefaultFont() = 0;
    /* the family name of a face GetRunFontFaceId gave (a BSTR) */
    virtual BSTR GetSerializableFontName(DWORD fontFaceId) = 0;
    /* the face of an id GetRunFontFaceId gave (AddRef'd) */
    virtual IDWriteFontFace *GetFontFace(DWORD fontFaceId) = 0;
    /* the face (its id, 0 for none) the family @pCurrentFontName gives the
     * text's first @runCount characters */
    virtual DWORD GetRunFontFaceId(const wchar_t *pCurrentFontName, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                                   DWRITE_FONT_STYLE style, LCID lcid, const wchar_t *pText, unsigned int charCount,
                                   DWORD fontFaceIdCurrent, unsigned int &runCount) = 0;
};
