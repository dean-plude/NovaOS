/* NovaOS: the Windows SDK's tom.h for Microsoft's XAML sources: MinGW-w64's
 * Text Object Model (ITextDocument, ITextRange, ITextSelection, ITextFont,
 * ITextPara, ITextStoryRanges, which are the Windows SDK's), with the
 * Windows 8 TOM 2 interfaces the XAML core's text boxes use (ITextDocument2,
 * ITextRange2, ITextSelection2, ITextFont2, ITextPara2) in place of the
 * pre-Windows 8 ITextDocument2 MinGW-w64 declares (another interface under
 * that name).  The methods are in the Windows SDK's order (as the Windows
 * metadata windows-rs is generated from lists them), the order NovaOS's
 * WinUIEdit.dll (userland/textserv) implements them in. */
#pragma once
#define ITextDocument2 nova_mingw_ITextDocument2
#define ITextDocument2Vtbl nova_mingw_ITextDocument2Vtbl
#define IID_ITextDocument2 nova_mingw_IID_ITextDocument2
#include_next <tom.h>
#undef ITextDocument2
#undef ITextDocument2Vtbl
#undef IID_ITextDocument2

#ifdef __cplusplus
struct ITextDocument2;
struct ITextRange2;
struct ITextSelection2;
struct ITextFont2;
struct ITextPara2;
struct ITextStoryRanges2;
struct ITextStory;
struct ITextStrings;
struct ITextRow;
struct ITextDisplays;

#ifndef tomAllowFinalEOP
#define tomAllowFinalEOP    8
#endif
#ifndef tomCanUndo
#define tomCanCopy          137
#define tomCanRedo          138
#define tomCanUndo          139
#define tomUndoLimit        140
#endif
#ifndef tomAllowOffClient
#define tomAllowOffClient   0x200
#endif
#ifndef tomCharRepFromLcid
#define tomGlyphs           0x40000
#define tomCharRepFromLcid  0x10000
#endif

struct DECLSPEC_UUID("c241f5e2-7206-11d8-a2c7-00a0d1d6c6b3") ITextRange2 : public ITextSelection {
    virtual HRESULT STDMETHODCALLTYPE GetCch(long *pcch) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCells(IUnknown **ppCells) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetColumn(IUnknown **ppColumn) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCount(long *pCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDuplicate2(ITextRange2 **ppRange) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFont2(ITextFont2 **ppFont) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetFont2(ITextFont2 *pFont) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFormattedText2(ITextRange2 **ppRange) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetFormattedText2(ITextRange2 *pRange) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetGravity(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetGravity(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPara2(ITextPara2 **ppPara) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPara2(ITextPara2 *pPara) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRow(ITextRow **ppRow) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStartPara(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTable(IUnknown **ppTable) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetURL(BSTR *pbstr) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetURL(BSTR bstr) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddSubrange(long cp1, long cp2, long Activate) = 0;
    virtual HRESULT STDMETHODCALLTYPE BuildUpMath(long Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteSubrange(long cpFirst, long cpLim) = 0;
    virtual HRESULT STDMETHODCALLTYPE Find(ITextRange2 *pRange, long Count, long Flags, long *pDelta) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChar2(long *pChar, long Offset) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDropCap(long *pcLine, long *pPosition) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetInlineObject(long *pType, long *pAlign, long *pChar, long *pChar1, long *pChar2,
                                                      long *pCount, long *pTeXStyle, long *pcCol, long *pLevel) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(long Type, long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRect(long Type, long *pLeft, long *pTop, long *pRight, long *pBottom, long *pHit) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSubrange(long iSubrange, long *pcpFirst, long *pcpLim) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetText2(long Flags, BSTR *pbstr) = 0;
    virtual HRESULT STDMETHODCALLTYPE HexToUnicode() = 0;
    virtual HRESULT STDMETHODCALLTYPE InsertTable(long cCol, long cRow, long AutoFit) = 0;
    virtual HRESULT STDMETHODCALLTYPE Linearize(long Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetActiveSubrange(long cpAnchor, long cpActive) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDropCap(long cLine, long Position) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProperty(long Type, long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetText2(long Flags, BSTR bstr) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnicodeToHex() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetInlineObject(long Type, long Align, long Char, long Char1, long Char2,
                                                      long Count, long TeXStyle, long cCol) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMathFunctionType(BSTR bstr, long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE InsertImage(long width, long height, long ascent, long Type, BSTR bstrAltText,
                                                  IStream *pStream) = 0;
};

struct DECLSPEC_UUID("c241f5e1-7206-11d8-a2c7-00a0d1d6c6b3") ITextSelection2 : public ITextRange2 {
};

struct DECLSPEC_UUID("c241f5e3-7206-11d8-a2c7-00a0d1d6c6b3") ITextFont2 : public ITextFont {
    virtual HRESULT STDMETHODCALLTYPE GetCount(long *pCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAutoLigatures(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetAutoLigatures(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAutospaceAlpha(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetAutospaceAlpha(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAutospaceNumeric(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetAutospaceNumeric(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAutospaceParens(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetAutospaceParens(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCharRep(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCharRep(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCompressionMode(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCompressionMode(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCookie(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCookie(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDoubleStrike(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDoubleStrike(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDuplicate2(ITextFont2 **ppFont) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDuplicate2(ITextFont2 *pFont) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLinkType(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMathZone(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMathZone(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetModWidthPairs(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetModWidthPairs(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetModWidthSpace(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetModWidthSpace(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetOldNumbers(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetOldNumbers(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetOverlapping(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetOverlapping(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPositionSubSuper(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPositionSubSuper(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetScaling(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetScaling(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSpaceExtension(float *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSpaceExtension(float Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUnderlinePositionMode(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetUnderlinePositionMode(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetEffects(long *pValue, long *pMask) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetEffects2(long *pValue, long *pMask) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(long Type, long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyInfo(long Index, long *pType, long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsEqual2(ITextFont2 *pFont, long *pB) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEffects(long Value, long Mask) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEffects2(long Value, long Mask) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProperty(long Type, long Value) = 0;
};

struct DECLSPEC_UUID("c241f5e4-7206-11d8-a2c7-00a0d1d6c6b3") ITextPara2 : public ITextPara {
    virtual HRESULT STDMETHODCALLTYPE GetBorders(IUnknown **ppBorders) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDuplicate2(ITextPara2 **ppPara) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDuplicate2(ITextPara2 *pPara) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFontAlignment(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetFontAlignment(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetHangingPunctuation(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetHangingPunctuation(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSnapToGrid(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSnapToGrid(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrimPunctuationAtStart(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTrimPunctuationAtStart(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetEffects(long *pValue, long *pMask) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(long Type, long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsEqual2(ITextPara2 *pPara, long *pB) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEffects(long Value, long Mask) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProperty(long Type, long Value) = 0;
};

struct DECLSPEC_UUID("c241f5e0-7206-11d8-a2c7-00a0d1d6c6b3") ITextDocument2 : public ITextDocument {
    virtual HRESULT STDMETHODCALLTYPE GetCaretType(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCaretType(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDisplays(ITextDisplays **ppDisplays) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDocumentFont(ITextFont2 **ppFont) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDocumentFont(ITextFont2 *pFont) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDocumentPara(ITextPara2 **ppPara) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDocumentPara(ITextPara2 *pPara) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetEastAsianFlags(long *pFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetGenerator(BSTR *pbstr) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetIMEInProgress(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNotificationMode(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetNotificationMode(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSelection2(ITextSelection2 **ppSel) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStoryRanges2(ITextStoryRanges2 **ppStories) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTypographyOptions(long *pOptions) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVersion(long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetWindow(__int64 *pHwnd) = 0;
    virtual HRESULT STDMETHODCALLTYPE AttachMsgFilter(IUnknown *pFilter) = 0;
    virtual HRESULT STDMETHODCALLTYPE CheckTextLimit(long cch, long *pcch) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCallManager(IUnknown **ppVoid) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClientRect(long Type, long *pLeft, long *pTop, long *pRight, long *pBottom) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetEffectColor(long Index, COLORREF *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetImmContext(__int64 *pContext) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPreferredFont(long cp, long CharRep, long Options, long curCharRep, long curFontSize,
                                                       BSTR *pbstr, long *pPitchAndFamily, long *pNewFontSize) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(long Type, long *pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStrings(ITextStrings **ppStrs) = 0;
    virtual HRESULT STDMETHODCALLTYPE Notify(long Notify) = 0;
    virtual HRESULT STDMETHODCALLTYPE Range2(long cpActive, long cpAnchor, ITextRange2 **ppRange) = 0;
    virtual HRESULT STDMETHODCALLTYPE RangeFromPoint2(long x, long y, long Type, ITextRange2 **ppRange) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReleaseCallManager(IUnknown *pVoid) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReleaseImmContext(__int64 Context) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEffectColor(long Index, COLORREF Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProperty(long Type, long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTypographyOptions(long Options, long Mask) = 0;
    virtual HRESULT STDMETHODCALLTYPE SysBeep() = 0;
    virtual HRESULT STDMETHODCALLTYPE Update(long Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE UpdateWindow() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMathProperties(long *pOptions) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMathProperties(long Options, long Mask) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetActiveStory(ITextStory **ppStory) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetActiveStory(ITextStory *pStory) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMainStory(ITextStory **ppStory) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNewStory(ITextStory **ppStory) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStory(long Index, ITextStory **ppStory) = 0;
};
#endif
