/* novatextserv.h — the windowless Rich Edit's interfaces, in C: ITextHost
 * and ITextHost2 (what a program hosting the text implements) and
 * ITextServices and ITextServices2 (what CreateTextServices gives it), the
 * structures they pass and the Rich Edit messages the text services
 * answer.  Written from the Rich Edit and text services documentation.
 * userland/textserv/textserv.c is the text services (riched20.dll,
 * msftedit.dll and WinUIEdit.dll); textservtest.c is a host.
 *
 * As on Windows, the two interfaces' methods past IUnknown use the
 * thiscall convention in 32-bit code (TXAPI): programs compiled with
 * Microsoft's textserv.h call them as C++ virtual functions. */
#pragma once
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>

#if defined(__i386__)
#define TXAPI __attribute__((thiscall))
#else
#define TXAPI
#endif

#ifndef NOVA_RECTL_DEFINED
#define NOVA_RECTL_DEFINED
typedef struct _RECTL { LONG left, top, right, bottom; } RECTL, *PRECTL, *LPRECTL;
typedef const RECTL *LPCRECTL;
typedef struct tagSIZE SIZEL, *PSIZEL, *LPSIZEL;
#endif
typedef void *HIMC_;

/* ---- structures -------------------------------------------------------- */
#ifndef NOVA_RICHEDIT_STRUCTS
#define NOVA_RICHEDIT_STRUCTS
typedef struct _charrange { LONG cpMin, cpMax; } CHARRANGE;
typedef struct _charformatw {
    UINT cbSize;
    DWORD dwMask, dwEffects;
    LONG yHeight, yOffset;                       /* twips */
    COLORREF crTextColor;
    BYTE bCharSet, bPitchAndFamily;
    WCHAR szFaceName[32];
} CHARFORMATW;
typedef struct _charformat2w {                   /* CHARFORMATW and then: */
    CHARFORMATW cf;
    WORD wWeight;
    SHORT sSpacing;
    COLORREF crBackColor;
    LCID lcid;
    DWORD dwReserved;
    SHORT sStyle;
    WORD wKerning;
    BYTE bUnderlineType, bAnimation, bRevAuthor, bUnderlineColor;
} CHARFORMAT2W;
typedef struct _paraformat {
    UINT cbSize;
    DWORD dwMask;
    WORD wNumbering, wEffects;
    LONG dxStartIndent, dxRightIndent, dxOffset;
    WORD wAlignment;
    SHORT cTabCount;
    LONG rgxTabs[32];
} PARAFORMAT;
typedef struct _selchange { NMHDR nmhdr; CHARRANGE chrg; WORD seltyp; } SELCHANGE;
typedef struct _gettextex { DWORD cb, flags; UINT codepage; LPCSTR lpDefaultChar; LPBOOL lpUsedDefChar; } GETTEXTEX;
typedef struct _gettextlengthex { DWORD flags; UINT codepage; } GETTEXTLENGTHEX;
typedef struct _settextex { DWORD flags; UINT codepage; } SETTEXTEX;
#endif

typedef enum { TXTBACK_TRANSPARENT = 0, TXTBACK_OPAQUE = 1 } TXTBACKSTYLE;

/* CHARFORMAT dwMask / dwEffects */
#define CFM_BOLD       0x00000001
#define CFM_ITALIC     0x00000002
#define CFM_UNDERLINE  0x00000004
#define CFM_SIZE       0x80000000
#define CFM_COLOR      0x40000000
#define CFM_FACE       0x20000000
#define CFE_BOLD       0x0001
#define CFE_ITALIC     0x0002
#define CFE_UNDERLINE  0x0004
#define CFE_AUTOCOLOR  0x40000000
#define CFM_WEIGHT     0x00400000

/* ITextHost::TxGetPropertyBits and OnTxPropertyBitsChange */
#define TXTBIT_RICHTEXT          0x00000001
#define TXTBIT_MULTILINE         0x00000002
#define TXTBIT_READONLY          0x00000004
#define TXTBIT_SHOWACCELERATOR   0x00000008
#define TXTBIT_USEPASSWORD       0x00000010
#define TXTBIT_HIDESELECTION     0x00000020
#define TXTBIT_SAVESELECTION     0x00000040
#define TXTBIT_AUTOWORDSEL       0x00000080
#define TXTBIT_VERTICAL          0x00000100
#define TXTBIT_SELBARCHANGE      0x00000200
#define TXTBIT_WORDWRAP          0x00000400
#define TXTBIT_ALLOWBEEP         0x00000800
#define TXTBIT_DISABLEDRAG       0x00001000
#define TXTBIT_VIEWINSETCHANGE   0x00002000
#define TXTBIT_BACKSTYLECHANGE   0x00004000
#define TXTBIT_MAXLENGTHCHANGE   0x00008000
#define TXTBIT_SCROLLBARCHANGE   0x00010000
#define TXTBIT_CHARFORMATCHANGE  0x00020000
#define TXTBIT_PARAFORMATCHANGE  0x00040000
#define TXTBIT_EXTENTCHANGE      0x00080000
#define TXTBIT_CLIENTRECTCHANGE  0x00100000
#define TXTBIT_USECURRENTBKG     0x00200000

/* ITextServices::TxGetNaturalSize modes, TxQueryHitPoint results, views */
#define TXTNS_FITTOCONTENT      1
#define TXTNS_ROUNDTOLINE       2
#define TXTNS_FITTOCONTENT2     0
#define TXTNS_FITTOCONTENTWSP   3
#define HITRESULT_OUTSIDE       0
#define HITRESULT_TRANSPARENT   1
#define HITRESULT_CLOSE         2
#define HITRESULT_HIT           3
#define TXTVIEW_ACTIVE          0
#define TXTVIEW_INACTIVE        (-1)

/* Rich Edit messages (beyond winuser.h's EM_*) and notifications */
#define EM_CANPASTE         (WM_USER + 50)
#define EM_EXGETSEL         (WM_USER + 52)
#define EM_EXLIMITTEXT      (WM_USER + 53)
#define EM_EXLINEFROMCHAR   (WM_USER + 54)
#define EM_EXSETSEL         (WM_USER + 55)
#define EM_GETCHARFORMAT    (WM_USER + 58)
#define EM_GETEVENTMASK     (WM_USER + 59)
#define EM_GETOLEINTERFACE  (WM_USER + 60)
#define EM_GETPARAFORMAT    (WM_USER + 61)
#define EM_GETSELTEXT       (WM_USER + 62)
#define EM_HIDESELECTION    (WM_USER + 63)
#define EM_SELECTIONTYPE    (WM_USER + 66)
#define EM_SETCHARFORMAT    (WM_USER + 68)
#define EM_SETEVENTMASK     (WM_USER + 69)
#define EM_SETOLECALLBACK   (WM_USER + 70)
#define EM_SETPARAFORMAT    (WM_USER + 71)
#define EM_GETTEXTRANGE     (WM_USER + 75)
#define EM_SETOPTIONS       (WM_USER + 77)
#define EM_GETOPTIONS       (WM_USER + 78)
#define EM_SETUNDOLIMIT     (WM_USER + 82)
#define EM_REDO             (WM_USER + 84)
#define EM_CANREDO          (WM_USER + 85)
#define EM_STOPGROUPTYPING  (WM_USER + 88)
#define EM_SETTEXTMODE      (WM_USER + 89)
#define EM_GETTEXTMODE      (WM_USER + 90)
#define EM_AUTOURLDETECT    (WM_USER + 91)
#define EM_GETTEXTEX        (WM_USER + 94)
#define EM_GETTEXTLENGTHEX  (WM_USER + 95)
#define EM_SETTEXTEX        (WM_USER + 97)
#define EM_SETLANGOPTIONS   (WM_USER + 120)
#define EM_GETLANGOPTIONS   (WM_USER + 121)
#define EM_SETEDITSTYLE     (WM_USER + 204)
#define EM_GETEDITSTYLE     (WM_USER + 205)

#define ENM_CHANGE          0x00000001
#define ENM_UPDATE          0x00000002
#define ENM_SELCHANGE       0x00080000
#define EN_MSGFILTER        0x0700
#define EN_SELCHANGE        0x0702
#define SEL_EMPTY           0x0000
#define SEL_TEXT            0x0001
#define GT_DEFAULT          0
#define GT_USECRLF          1
#define GT_SELECTION        2
#define GTL_DEFAULT         0
#define GTL_NUMCHARS        8
#define ST_DEFAULT          0
#define ST_KEEPUNDO         1
#define ST_SELECTION        2
#define TM_PLAINTEXT        1
#define TM_RICHTEXT         2

/* ---- the interfaces ---------------------------------------------------- */
typedef struct ITextHost2 ITextHost2;
typedef ITextHost2 ITextHost;
typedef struct ITextServices2 ITextServices2;
typedef ITextServices2 ITextServices;

typedef struct ITextHost2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ITextHost2 *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ITextHost2 *);
    ULONG (STDMETHODCALLTYPE *Release)(ITextHost2 *);
    /* ITextHost */
    HDC (TXAPI *TxGetDC)(ITextHost2 *);
    INT (TXAPI *TxReleaseDC)(ITextHost2 *, HDC);
    BOOL (TXAPI *TxShowScrollBar)(ITextHost2 *, INT bar, BOOL show);
    BOOL (TXAPI *TxEnableScrollBar)(ITextHost2 *, INT flags, INT arrows);
    BOOL (TXAPI *TxSetScrollRange)(ITextHost2 *, INT bar, LONG minpos, INT maxpos, BOOL redraw);
    BOOL (TXAPI *TxSetScrollPos)(ITextHost2 *, INT bar, INT pos, BOOL redraw);
    void (TXAPI *TxInvalidateRect)(ITextHost2 *, LPCRECT rc, BOOL mode);
    void (TXAPI *TxViewChange)(ITextHost2 *, BOOL update);
    BOOL (TXAPI *TxCreateCaret)(ITextHost2 *, HBITMAP bmp, INT width, INT height);
    BOOL (TXAPI *TxShowCaret)(ITextHost2 *, BOOL show);
    BOOL (TXAPI *TxSetCaretPos)(ITextHost2 *, INT x, INT y);
    BOOL (TXAPI *TxSetTimer)(ITextHost2 *, UINT id, UINT timeout);
    void (TXAPI *TxKillTimer)(ITextHost2 *, UINT id);
    void (TXAPI *TxScrollWindowEx)(ITextHost2 *, INT dx, INT dy, LPCRECT scroll, LPCRECT clip, HRGN update, LPRECT updaterc, UINT flags);
    void (TXAPI *TxSetCapture)(ITextHost2 *, BOOL capture);
    void (TXAPI *TxSetFocus)(ITextHost2 *);
    void (TXAPI *TxSetCursor)(ITextHost2 *, HCURSOR cursor, BOOL text);
    BOOL (TXAPI *TxScreenToClient)(ITextHost2 *, LPPOINT pt);
    BOOL (TXAPI *TxClientToScreen)(ITextHost2 *, LPPOINT pt);
    HRESULT (TXAPI *TxActivate)(ITextHost2 *, LONG *old);
    HRESULT (TXAPI *TxDeactivate)(ITextHost2 *, LONG state);
    HRESULT (TXAPI *TxGetClientRect)(ITextHost2 *, LPRECT rc);
    HRESULT (TXAPI *TxGetViewInset)(ITextHost2 *, LPRECT inset);
    HRESULT (TXAPI *TxGetCharFormat)(ITextHost2 *, const CHARFORMATW **cf);
    HRESULT (TXAPI *TxGetParaFormat)(ITextHost2 *, const PARAFORMAT **pf);
    COLORREF (TXAPI *TxGetSysColor)(ITextHost2 *, int index);
    HRESULT (TXAPI *TxGetBackStyle)(ITextHost2 *, TXTBACKSTYLE *style);
    HRESULT (TXAPI *TxGetMaxLength)(ITextHost2 *, DWORD *len);
    HRESULT (TXAPI *TxGetScrollBars)(ITextHost2 *, DWORD *bars);
    HRESULT (TXAPI *TxGetPasswordChar)(ITextHost2 *, WCHAR *ch);
    HRESULT (TXAPI *TxGetAcceleratorPos)(ITextHost2 *, LONG *cp);
    HRESULT (TXAPI *TxGetExtent)(ITextHost2 *, LPSIZEL extent);
    HRESULT (TXAPI *OnTxCharFormatChange)(ITextHost2 *, const CHARFORMATW *cf);
    HRESULT (TXAPI *OnTxParaFormatChange)(ITextHost2 *, const PARAFORMAT *pf);
    HRESULT (TXAPI *TxGetPropertyBits)(ITextHost2 *, DWORD mask, DWORD *bits);
    HRESULT (TXAPI *TxNotify)(ITextHost2 *, DWORD code, void *data);
    HIMC_ (TXAPI *TxImmGetContext)(ITextHost2 *);
    void (TXAPI *TxImmReleaseContext)(ITextHost2 *, HIMC_ himc);
    HRESULT (TXAPI *TxGetSelectionBarWidth)(ITextHost2 *, LONG *width);
    /* ITextHost2 (a host QueryInterface answers IID_ITextHost2 for) */
    BOOL (TXAPI *TxIsDoubleClickPending)(ITextHost2 *);
    HRESULT (TXAPI *TxGetWindow)(ITextHost2 *, HWND *hwnd);
    HRESULT (TXAPI *TxSetForegroundWindow)(ITextHost2 *);
    HPALETTE (TXAPI *TxGetPalette)(ITextHost2 *);
    HRESULT (TXAPI *TxGetEastAsianFlags)(ITextHost2 *, LONG *flags);
    HCURSOR (TXAPI *TxSetCursor2)(ITextHost2 *, HCURSOR cursor, BOOL text);
    void (TXAPI *TxFreeTextServicesNotification)(ITextHost2 *);
    HRESULT (TXAPI *TxGetEditStyle)(ITextHost2 *, DWORD item, DWORD *data);
    HRESULT (TXAPI *TxGetWindowStyles)(ITextHost2 *, DWORD *style, DWORD *exstyle);
    HRESULT (TXAPI *TxShowDropCaret)(ITextHost2 *, BOOL show, HDC hdc, LPCRECT rc);
    HRESULT (TXAPI *TxDestroyCaret)(ITextHost2 *);
    HRESULT (TXAPI *TxGetHorzExtent)(ITextHost2 *, LONG *extent);
} ITextHost2Vtbl;
struct ITextHost2 { const ITextHost2Vtbl *lpVtbl; };

typedef BOOL (CALLBACK *TXCONTINUE)(DWORD);
typedef struct ID2D1RenderTarget_ ID2D1RenderTarget_;

typedef struct ITextServices2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ITextServices2 *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ITextServices2 *);
    ULONG (STDMETHODCALLTYPE *Release)(ITextServices2 *);
    /* ITextServices */
    HRESULT (TXAPI *TxSendMessage)(ITextServices2 *, UINT msg, WPARAM wp, LPARAM lp, LRESULT *result);
    HRESULT (TXAPI *TxDraw)(ITextServices2 *, DWORD aspect, LONG lindex, void *aspectinfo, DVTARGETDEVICE *td,
                            HDC hdc, HDC target, LPCRECTL bounds, LPCRECTL wbounds, LPRECT update,
                            TXCONTINUE cont, DWORD contparam, LONG view);
    HRESULT (TXAPI *TxGetHScroll)(ITextServices2 *, LONG *min, LONG *max, LONG *pos, LONG *page, BOOL *enabled);
    HRESULT (TXAPI *TxGetVScroll)(ITextServices2 *, LONG *min, LONG *max, LONG *pos, LONG *page, BOOL *enabled);
    HRESULT (TXAPI *OnTxSetCursor)(ITextServices2 *, DWORD aspect, LONG lindex, void *aspectinfo, DVTARGETDEVICE *td,
                                   HDC hdc, HDC target, LPCRECT client, INT x, INT y);
    HRESULT (TXAPI *TxQueryHitPoint)(ITextServices2 *, DWORD aspect, LONG lindex, void *aspectinfo, DVTARGETDEVICE *td,
                                     HDC hdc, HDC target, LPCRECT client, INT x, INT y, DWORD *hit);
    HRESULT (TXAPI *OnTxInPlaceActivate)(ITextServices2 *, LPCRECT client);
    HRESULT (TXAPI *OnTxInPlaceDeactivate)(ITextServices2 *);
    HRESULT (TXAPI *OnTxUIActivate)(ITextServices2 *);
    HRESULT (TXAPI *OnTxUIDeactivate)(ITextServices2 *);
    HRESULT (TXAPI *TxGetText)(ITextServices2 *, BSTR *text);
    HRESULT (TXAPI *TxSetText)(ITextServices2 *, LPCWSTR text);
    HRESULT (TXAPI *TxGetCurTargetX)(ITextServices2 *, LONG *x);
    HRESULT (TXAPI *TxGetBaseLinePos)(ITextServices2 *, LONG *pos);
    HRESULT (TXAPI *TxGetNaturalSize)(ITextServices2 *, DWORD aspect, HDC hdc, HDC target, DVTARGETDEVICE *td,
                                      DWORD mode, const SIZEL *extent, LONG *width, LONG *height);
    HRESULT (TXAPI *TxGetDropTarget)(ITextServices2 *, IDropTarget **target);
    HRESULT (TXAPI *OnTxPropertyBitsChange)(ITextServices2 *, DWORD mask, DWORD bits);
    HRESULT (TXAPI *TxGetCachedSize)(ITextServices2 *, DWORD *width, DWORD *height);
    /* ITextServices2 */
    HRESULT (TXAPI *TxGetNaturalSize2)(ITextServices2 *, DWORD aspect, HDC hdc, HDC target, DVTARGETDEVICE *td,
                                       DWORD mode, const SIZEL *extent, LONG *width, LONG *height, LONG *ascent);
    HRESULT (TXAPI *TxDrawD2D)(ITextServices2 *, ID2D1RenderTarget_ *rt, LPCRECTL bounds, LPRECT update, LONG view);
} ITextServices2Vtbl;
struct ITextServices2 { const ITextServices2Vtbl *lpVtbl; };

/* the IIDs (riched20.dll, msftedit.dll and WinUIEdit.dll export them as data) */
#define NOVA_IID_ITextServices   {0x8d33f740, 0xcf58, 0x11ce, {0xa8, 0x9d, 0x00, 0xaa, 0x00, 0x6c, 0xad, 0xc5}}
#define NOVA_IID_ITextServices2  {0x8d33f741, 0xcf58, 0x11ce, {0xa8, 0x9d, 0x00, 0xaa, 0x00, 0x6c, 0xad, 0xc5}}
#define NOVA_IID_ITextHost       {0xc5bdd8d0, 0xd26e, 0x11ce, {0xa8, 0x9e, 0x00, 0xaa, 0x00, 0x6c, 0xad, 0xc5}}
#define NOVA_IID_ITextHost2      {0x13e670f5, 0x1a5a, 0x11cf, {0xab, 0xeb, 0x00, 0xaa, 0x00, 0xb6, 0x5e, 0xa1}}
#define NOVA_IID_ITextDocument2  {0xc241f5e0, 0x7206, 0x11d8, {0xa2, 0xc7, 0x00, 0xa0, 0xd1, 0xd6, 0xc6, 0xb3}}
#define NOVA_IID_IRicheditWindowlessAccessibility {0x983e572d, 0x20cd, 0x460b, {0x91, 0x04, 0x83, 0x11, 0x15, 0x92, 0xdd, 0x10}}
#define NOVA_IID_IRichEditOle    {0x00020d00, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}}
#define NOVA_IID_IRichEditOleCallback {0x00020d03, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}}

typedef HRESULT (WINAPI *PCREATETEXTSERVICES)(IUnknown *outer, ITextHost *host, IUnknown **out);
typedef HRESULT (WINAPI *PSHUTDOWNTEXTSERVICES)(IUnknown *services);
