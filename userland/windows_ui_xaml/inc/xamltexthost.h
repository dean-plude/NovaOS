/* NovaOS: xamltexthost.h, the Windows App SDK's (closed) header for the
 * extra host interfaces XAML's text boxes answer to the windowless Rich
 * Edit (core/native/text/Controls/TextServicesHost.cpp): IXamlTextHost (the
 * view's viewport and padding) and IGripperHost2 (touch selection
 * grippers).  The methods in the order XAML's host declares them; the IIDs
 * are NovaOS's (NovaOS's WinUIEdit.dll, userland/textserv, asks for
 * neither yet). */
#pragma once

struct DECLSPEC_UUID("8db2a30b-8488-49ed-8b60-d96842156cb2") IXamlTextHost : public IUnknown {
    virtual HRESULT TxGetViewportRect(RECT *pViewportRect) = 0;
    virtual HRESULT TxGetContentPadding(RECT *pContentPadding) = 0;
    virtual bool TxGetShouldUseVisualPixels() = 0;
};

struct DECLSPEC_UUID("00f8d24e-99ad-411e-95a6-87dd18c05fc1") IGripperHost2 : public IUnknown {
    virtual HRESULT ShowGrippers(POINT *pBeginPoint, float beginLineHeight, POINT *pEndPoint, float endLineHeight) = 0;
    virtual HRESULT GetVisibleRect(RECT *pRect) = 0;
    virtual HRESULT EnsureRectVisible(const RECT &rect) = 0;
    virtual HRESULT EnsureRectVisibleWithPadding(const RECT &rect) = 0;
    virtual HRESULT GetGripperMetrics(int *pnDiameter, int *pnOffsetBelowLine) = 0;
};
