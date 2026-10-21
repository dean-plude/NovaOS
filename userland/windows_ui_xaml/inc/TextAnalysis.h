/* NovaOS: the Windows App SDK's TextAnalysis.h (FrameworkUdk, shipped
 * closed), as far as XAML's text core names it: the text analysis source
 * that also gives a locale name list (ITextFormatCallbacks, a DirectWrite
 * IDWriteTextAnalysisSource with two members more, which XAML's
 * StackTextAnalysisSource implements, core/text/RichTextServices/
 * TextFormatter/TextSegment.h), a text format's locale name list and a
 * text's reading direction detected from its content (core/text/TextBlock).
 * The two functions are FrameworkUdk's, which NovaOS does not have yet:
 * they stand in as unbuilt (build.py unbuilt()), so a text block keeps its
 * language and a direction detected from content falls back as XAML's does
 * when detection fails. */
#pragma once
#include <dwrite_2.h>
#include <stdint.h>

namespace ABI { namespace Microsoft { namespace Internal { namespace FrameworkUdk {
struct ITextFormatCallbacks : public IDWriteTextAnalysisSource
{
    virtual BOOL STDMETHODCALLTYPE SupportsGetLocaleNameList() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLocaleNameList(uint32_t textPosition, uint32_t *textLength,
                                                        wchar_t const **localeName) = 0;
};
}}}}

HRESULT TextAnalysis_SetLocaleNameList(IDWriteTextFormat *textFormat, wchar_t const *localeNameList);
HRESULT TextAnalysis_GetContentReadingDirection(IDWriteTextAnalyzer *analyzer, IDWriteTextAnalysisSource *source,
                                                UINT32 textPosition, UINT32 textLength,
                                                DWRITE_READING_DIRECTION *readingDirection, BOOL *isAmbiguous);
