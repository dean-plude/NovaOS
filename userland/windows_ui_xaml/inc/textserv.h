/* NovaOS: the Windows SDK's textserv.h for Microsoft's XAML sources:
 * MinGW-w64's windowless Rich Edit interfaces (ITextServices, ITextHost),
 * with the second versions the XAML core's text boxes host and are hosted
 * by (ITextServices2, ITextHost2) and the flags they pass, which MinGW-w64
 * leaves out.  Methods in the Windows SDK's order (the order NovaOS's
 * WinUIEdit.dll, userland/textserv, implements them in:
 * userland/include/novatextserv.h). */
#pragma once
#include_next <textserv.h>

/* (TXTNS_FITTOCONTENTWSP and TXTNS_INCLUDELASTLINE XAML's text box view
 * declares itself) */
#ifndef TXTNS_FITTOCONTENT2
#define TXTNS_FITTOCONTENT2         0
#define TXTNS_FITTOCONTENT3         3
#define TXTNS_EMU                   0x80000000
#endif
#ifndef TXTBIT_NOTHREADREFCOUNT
#define TXTBIT_NOTHREADREFCOUNT     0x00400000
#define TXTBIT_SHOWPASSWORD         0x00800000
#define TXTBIT_D2DDWRITE            0x01000000
#define TXTBIT_D2DSIMPLETYPOGRAPHY  0x02000000
#define TXTBIT_D2DPIXELSNAPPED      0x04000000
#define TXTBIT_D2DSUBPIXELLINES     0x08000000
#define TXTBIT_FLASHLASTPASSWORDCHAR 0x10000000
#define TXTBIT_ADVANCEDINPUT        0x20000000
#endif
#ifndef TXES_ISDIALOG
#define TXES_ISDIALOG               1
#endif

#ifdef __cplusplus
struct ID2D1RenderTarget;

class ITextHost2 : public ITextHost {
public:
    virtual WINBOOL TxIsDoubleClickPending() = 0;
    virtual HRESULT TxGetWindow(HWND *phwnd) = 0;
    virtual HRESULT TxSetForegroundWindow() = 0;
    virtual HPALETTE TxGetPalette() = 0;
    virtual HRESULT TxGetEastAsianFlags(LONG *pFlags) = 0;
    virtual HCURSOR TxSetCursor2(HCURSOR hcur, WINBOOL bText) = 0;
    virtual void TxFreeTextServicesNotification() = 0;
    virtual HRESULT TxGetEditStyle(DWORD dwItem, DWORD *pdwData) = 0;
    virtual HRESULT TxGetWindowStyles(DWORD *pdwStyle, DWORD *pdwExStyle) = 0;
    virtual HRESULT TxShowDropCaret(WINBOOL fShow, HDC hdc, LPCRECT prc) = 0;
    virtual HRESULT TxDestroyCaret() = 0;
    virtual HRESULT TxGetHorzExtent(LONG *plHorzExtent) = 0;
};

class ITextServices2 : public ITextServices {
public:
    virtual HRESULT TxGetNaturalSize2(DWORD dwAspect, HDC hdcDraw, HDC hicTargetDev, DVTARGETDEVICE *ptd, DWORD dwMode,
                                      const SIZEL *psizelExtent, LONG *pwidth, LONG *pheight, LONG *pascent) = 0;
    virtual HRESULT TxDrawD2D(ID2D1RenderTarget *pRenderTarget, LPCRECTL lprcBounds, LPRECT lprcUpdate, LONG lViewId) = 0;
};
#endif
