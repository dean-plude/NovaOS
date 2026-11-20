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
 * Microsoft's textserv.h call them as C++ virtual functions.  The Text
 * Object Model's interfaces (ITextDocument2, ITextRange2, ITextSelection2;
 * tom.h on Windows) are dual interfaces with stdcall methods. */
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
typedef struct _changenotify { DWORD dwChangeType; void *pvCookieData; } CHANGENOTIFY;   /* EN_CHANGE's, windowless */
#define CN_GENERIC          0
#define CN_TEXTCHANGED      1
#define CN_NEWUNDO          2
#define CN_NEWREDO          4
typedef struct _gettextex { DWORD cb, flags; UINT codepage; LPCSTR lpDefaultChar; LPBOOL lpUsedDefChar; } GETTEXTEX;
typedef struct _gettextlengthex { DWORD flags; UINT codepage; } GETTEXTLENGTHEX;
typedef struct _settextex { DWORD flags; UINT codepage; } SETTEXTEX;
#endif

typedef enum { TXTBACK_TRANSPARENT = 0, TXTBACK_OPAQUE = 1 } TXTBACKSTYLE;

/* CHARFORMAT dwMask / dwEffects */
#define CFM_BOLD       0x00000001
#define CFM_ITALIC     0x00000002
#define CFM_UNDERLINE  0x00000004
#define CFM_STRIKEOUT  0x00000008
#define CFM_SIZE       0x80000000
#define CFM_COLOR      0x40000000
#define CFM_FACE       0x20000000
#define CFE_BOLD       0x0001
#define CFE_ITALIC     0x0002
#define CFE_UNDERLINE  0x0004
#define CFE_STRIKEOUT  0x0008
#define CFE_AUTOCOLOR  0x40000000
#define CFM_WEIGHT     0x00400000
/* EM_SETCHARFORMAT / EM_GETCHARFORMAT wParam */
#ifndef SCF_SELECTION
#define SCF_DEFAULT    0x0000
#define SCF_SELECTION  0x0001
#define SCF_WORD       0x0002
#define SCF_ALL        0x0004
#endif
/* paragraph formats (EM_SETPARAFORMAT, EM_GETPARAFORMAT) */
#ifndef PFM_ALIGNMENT
#define PFM_ALIGNMENT  0x00000008
#define PFA_LEFT       1
#define PFA_RIGHT      2
#define PFA_CENTER     3
#define PFA_JUSTIFY    4
#endif
#ifndef PFM_STARTINDENT
#define PFM_STARTINDENT  0x00000001
#define PFM_RIGHTINDENT  0x00000002
#define PFM_OFFSET       0x00000004
#define PFM_OFFSETINDENT 0x80000000
#endif
#ifndef SPF_SETDEFAULT
#define SPF_SETDEFAULT 0x0004
#endif

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
/* WinUIEdit's pointer input message (NovaOS's value, as XAML's text boxes
 * are built with: userland/windows_ui_xaml/inc/WinUIEdit.h): the mouse
 * message @msg at @pt, the view's coordinates */
#define NOVA_EM_MOUSEINPUT  (WM_USER + 0x175)
typedef struct { UINT msg; WPARAM wparam; POINT pt, screenPtInRawPixels; BOOL isPenInput; } NOVA_RE_MOUSEINPUT;
/* (and its DPI, which XAML sets to 96 and checks the answer of: NovaOS's
 * text services draw at the host's pixels and answer TRUE) */
#define NOVA_EM_SETDPI      (WM_USER + 0x17d)
/* a RichEditBox's formatting keys allowed (lp: CFE_BOLD, CFE_ITALIC, CFE_UNDERLINE) */
#define NOVA_EM_SETFORMATACCELERATORS (WM_USER + 0x181)

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

/* ---- the Text Object Model ---------------------------------------------- */
/* ITextDocument2 (QueryInterface on the text services), its ranges
 * (ITextRange2) and selection (ITextSelection2, which adds nothing to
 * ITextRange2), and IRichEditOle (EM_GETOLEINTERFACE).  These are dual
 * interfaces whose methods use the stdcall convention in 32-bit code,
 * unlike ITextServices' */
#define tomFalse            0
#define tomTrue             (-1)
#define tomUndefined        (-9999999)
#define tomToggle           (-9999998)
#define tomDefault          (-9999996)
#define tomSuspend          (-9999995)
#define tomResume           (-9999994)
#define tomForward          0x3fffffff
#define tomBackward         (-0x3fffffff)
#define tomMove             0
#define tomExtend           1
#define tomNoSelection      0
#define tomSelectionIP      1
#define tomSelectionNormal  2
#define tomSelStartActive   1
#define tomSelAtEOL         2
#define tomSelOvertype      4
#define tomSelActive        8
#define tomSelReplace       16
#define tomEnd              0
#define tomStart            32
#define tomClientCoord      256
#define tomAllowOffClient   512
#define tomTransform        1024
#define tomCharacter        1
#define tomWord             2
#define tomSentence         3
#define tomParagraph        4
#define tomLine             5
#define tomStory            6
#define tomScreen           7
#define tomMainTextStory    1
#define tomMatchWord        2
#define tomMatchCase        4
#define tomLowerCase        0
#define tomUpperCase        1
#define tomToggleCase       5
#define tomAdjustCRLF       1
#define tomUseCRLF          2
#define tomAllowFinalEOP    8
#define tomCaretTypeNormal  0

typedef struct ITextRange2 ITextRange2;
typedef ITextRange2 ITextSelection2, ITextRange, ITextSelection;
typedef struct ITextDocument2 ITextDocument2;
typedef ITextDocument2 ITextDocument;
typedef struct IRichEditOle_ IRichEditOle_;

typedef struct ITextRange2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ITextRange2 *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ITextRange2 *);
    ULONG (STDMETHODCALLTYPE *Release)(ITextRange2 *);
    /* IDispatch */
    HRESULT (STDMETHODCALLTYPE *GetTypeInfoCount)(ITextRange2 *, UINT *);
    HRESULT (STDMETHODCALLTYPE *GetTypeInfo)(ITextRange2 *, UINT, LCID, void **);
    HRESULT (STDMETHODCALLTYPE *GetIDsOfNames)(ITextRange2 *, REFIID, LPOLESTR *, UINT, LCID, LONG *);
    HRESULT (STDMETHODCALLTYPE *Invoke)(ITextRange2 *, LONG, REFIID, LCID, WORD, void *, VARIANT *, void *, UINT *);
    /* ITextRange */
    HRESULT (STDMETHODCALLTYPE *GetText)(ITextRange2 *, BSTR *);
    HRESULT (STDMETHODCALLTYPE *SetText)(ITextRange2 *, BSTR);
    HRESULT (STDMETHODCALLTYPE *GetChar)(ITextRange2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *SetChar)(ITextRange2 *, LONG);
    HRESULT (STDMETHODCALLTYPE *GetDuplicate)(ITextRange2 *, ITextRange **);
    HRESULT (STDMETHODCALLTYPE *GetFormattedText)(ITextRange2 *, ITextRange **);
    HRESULT (STDMETHODCALLTYPE *SetFormattedText)(ITextRange2 *, ITextRange *);
    HRESULT (STDMETHODCALLTYPE *GetStart)(ITextRange2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *SetStart)(ITextRange2 *, LONG);
    HRESULT (STDMETHODCALLTYPE *GetEnd)(ITextRange2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *SetEnd)(ITextRange2 *, LONG);
    HRESULT (STDMETHODCALLTYPE *GetFont)(ITextRange2 *, void **);
    HRESULT (STDMETHODCALLTYPE *SetFont)(ITextRange2 *, void *);
    HRESULT (STDMETHODCALLTYPE *GetPara)(ITextRange2 *, void **);
    HRESULT (STDMETHODCALLTYPE *SetPara)(ITextRange2 *, void *);
    HRESULT (STDMETHODCALLTYPE *GetStoryLength)(ITextRange2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *GetStoryType)(ITextRange2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *Collapse)(ITextRange2 *, LONG bStart);
    HRESULT (STDMETHODCALLTYPE *Expand)(ITextRange2 *, LONG unit, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *GetIndex)(ITextRange2 *, LONG unit, LONG *index);
    HRESULT (STDMETHODCALLTYPE *SetIndex)(ITextRange2 *, LONG unit, LONG index, LONG extend);
    HRESULT (STDMETHODCALLTYPE *SetRange)(ITextRange2 *, LONG anchor, LONG active);
    HRESULT (STDMETHODCALLTYPE *InRange)(ITextRange2 *, ITextRange *, LONG *);
    HRESULT (STDMETHODCALLTYPE *InStory)(ITextRange2 *, ITextRange *, LONG *);
    HRESULT (STDMETHODCALLTYPE *IsEqual)(ITextRange2 *, ITextRange *, LONG *);
    HRESULT (STDMETHODCALLTYPE *Select)(ITextRange2 *);
    HRESULT (STDMETHODCALLTYPE *StartOf)(ITextRange2 *, LONG unit, LONG extend, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *EndOf)(ITextRange2 *, LONG unit, LONG extend, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *Move)(ITextRange2 *, LONG unit, LONG count, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *MoveStart)(ITextRange2 *, LONG unit, LONG count, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *MoveEnd)(ITextRange2 *, LONG unit, LONG count, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *MoveWhile)(ITextRange2 *, VARIANT *cset, LONG count, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *MoveStartWhile)(ITextRange2 *, VARIANT *cset, LONG count, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *MoveEndWhile)(ITextRange2 *, VARIANT *cset, LONG count, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *MoveUntil)(ITextRange2 *, VARIANT *cset, LONG count, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *MoveStartUntil)(ITextRange2 *, VARIANT *cset, LONG count, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *MoveEndUntil)(ITextRange2 *, VARIANT *cset, LONG count, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *FindText)(ITextRange2 *, BSTR, LONG count, LONG flags, LONG *length);
    HRESULT (STDMETHODCALLTYPE *FindTextStart)(ITextRange2 *, BSTR, LONG count, LONG flags, LONG *length);
    HRESULT (STDMETHODCALLTYPE *FindTextEnd)(ITextRange2 *, BSTR, LONG count, LONG flags, LONG *length);
    HRESULT (STDMETHODCALLTYPE *Delete)(ITextRange2 *, LONG unit, LONG count, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *Cut)(ITextRange2 *, VARIANT *);
    HRESULT (STDMETHODCALLTYPE *Copy)(ITextRange2 *, VARIANT *);
    HRESULT (STDMETHODCALLTYPE *Paste)(ITextRange2 *, VARIANT *, LONG format);
    HRESULT (STDMETHODCALLTYPE *CanPaste)(ITextRange2 *, VARIANT *, LONG format, LONG *);
    HRESULT (STDMETHODCALLTYPE *CanEdit)(ITextRange2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *ChangeCase)(ITextRange2 *, LONG type);
    HRESULT (STDMETHODCALLTYPE *GetPoint)(ITextRange2 *, LONG type, LONG *x, LONG *y);
    HRESULT (STDMETHODCALLTYPE *SetPoint)(ITextRange2 *, LONG x, LONG y, LONG type, LONG extend);
    HRESULT (STDMETHODCALLTYPE *ScrollIntoView)(ITextRange2 *, LONG value);
    HRESULT (STDMETHODCALLTYPE *GetEmbeddedObject)(ITextRange2 *, IUnknown **);
    /* ITextSelection */
    HRESULT (STDMETHODCALLTYPE *GetFlags)(ITextRange2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *SetFlags)(ITextRange2 *, LONG);
    HRESULT (STDMETHODCALLTYPE *GetType)(ITextRange2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *MoveLeft)(ITextRange2 *, LONG unit, LONG count, LONG extend, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *MoveRight)(ITextRange2 *, LONG unit, LONG count, LONG extend, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *MoveUp)(ITextRange2 *, LONG unit, LONG count, LONG extend, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *MoveDown)(ITextRange2 *, LONG unit, LONG count, LONG extend, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *HomeKey)(ITextRange2 *, LONG unit, LONG extend, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *EndKey)(ITextRange2 *, LONG unit, LONG extend, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *TypeText)(ITextRange2 *, BSTR);
    /* ITextRange2 */
    HRESULT (STDMETHODCALLTYPE *GetCch)(ITextRange2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *GetCells)(ITextRange2 *, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *GetColumn)(ITextRange2 *, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *GetCount)(ITextRange2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *GetDuplicate2)(ITextRange2 *, ITextRange2 **);
    HRESULT (STDMETHODCALLTYPE *GetFont2)(ITextRange2 *, void **);
    HRESULT (STDMETHODCALLTYPE *SetFont2)(ITextRange2 *, void *);
    HRESULT (STDMETHODCALLTYPE *GetFormattedText2)(ITextRange2 *, ITextRange2 **);
    HRESULT (STDMETHODCALLTYPE *SetFormattedText2)(ITextRange2 *, ITextRange2 *);
    HRESULT (STDMETHODCALLTYPE *GetGravity)(ITextRange2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *SetGravity)(ITextRange2 *, LONG);
    HRESULT (STDMETHODCALLTYPE *GetPara2)(ITextRange2 *, void **);
    HRESULT (STDMETHODCALLTYPE *SetPara2)(ITextRange2 *, void *);
    HRESULT (STDMETHODCALLTYPE *GetRow)(ITextRange2 *, void **);
    HRESULT (STDMETHODCALLTYPE *GetStartPara)(ITextRange2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *GetTable)(ITextRange2 *, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *GetURL)(ITextRange2 *, BSTR *);
    HRESULT (STDMETHODCALLTYPE *SetURL)(ITextRange2 *, BSTR);
    HRESULT (STDMETHODCALLTYPE *AddSubrange)(ITextRange2 *, LONG cp1, LONG cp2, LONG activate);
    HRESULT (STDMETHODCALLTYPE *BuildUpMath)(ITextRange2 *, LONG flags);
    HRESULT (STDMETHODCALLTYPE *DeleteSubrange)(ITextRange2 *, LONG first, LONG lim);
    HRESULT (STDMETHODCALLTYPE *Find)(ITextRange2 *, ITextRange2 *, LONG count, LONG flags, LONG *delta);
    HRESULT (STDMETHODCALLTYPE *GetChar2)(ITextRange2 *, LONG *ch, LONG offset);
    HRESULT (STDMETHODCALLTYPE *GetDropCap)(ITextRange2 *, LONG *lines, LONG *position);
    HRESULT (STDMETHODCALLTYPE *GetInlineObject)(ITextRange2 *, LONG *, LONG *, LONG *, LONG *, LONG *, LONG *, LONG *, LONG *, LONG *);
    HRESULT (STDMETHODCALLTYPE *GetProperty)(ITextRange2 *, LONG type, LONG *value);
    HRESULT (STDMETHODCALLTYPE *GetRect)(ITextRange2 *, LONG type, LONG *left, LONG *top, LONG *right, LONG *bottom, LONG *hit);
    HRESULT (STDMETHODCALLTYPE *GetSubrange)(ITextRange2 *, LONG i, LONG *first, LONG *lim);
    HRESULT (STDMETHODCALLTYPE *GetText2)(ITextRange2 *, LONG flags, BSTR *);
    HRESULT (STDMETHODCALLTYPE *HexToUnicode)(ITextRange2 *);
    HRESULT (STDMETHODCALLTYPE *InsertTable)(ITextRange2 *, LONG cols, LONG rows, LONG autofit);
    HRESULT (STDMETHODCALLTYPE *Linearize)(ITextRange2 *, LONG flags);
    HRESULT (STDMETHODCALLTYPE *SetActiveSubrange)(ITextRange2 *, LONG anchor, LONG active);
    HRESULT (STDMETHODCALLTYPE *SetDropCap)(ITextRange2 *, LONG lines, LONG position);
    HRESULT (STDMETHODCALLTYPE *SetProperty)(ITextRange2 *, LONG type, LONG value);
    HRESULT (STDMETHODCALLTYPE *SetText2)(ITextRange2 *, LONG flags, BSTR);
    HRESULT (STDMETHODCALLTYPE *UnicodeToHex)(ITextRange2 *);
    HRESULT (STDMETHODCALLTYPE *SetInlineObject)(ITextRange2 *, LONG, LONG, LONG, LONG, LONG, LONG, LONG, LONG);
    HRESULT (STDMETHODCALLTYPE *GetMathFunctionType)(ITextRange2 *, BSTR, LONG *);
    HRESULT (STDMETHODCALLTYPE *InsertImage)(ITextRange2 *, LONG width, LONG height, LONG ascent, LONG type, BSTR alt, IStream *);
} ITextRange2Vtbl;
struct ITextRange2 { const ITextRange2Vtbl *lpVtbl; };

typedef struct ITextDocument2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ITextDocument2 *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(ITextDocument2 *);
    ULONG (STDMETHODCALLTYPE *Release)(ITextDocument2 *);
    /* IDispatch */
    HRESULT (STDMETHODCALLTYPE *GetTypeInfoCount)(ITextDocument2 *, UINT *);
    HRESULT (STDMETHODCALLTYPE *GetTypeInfo)(ITextDocument2 *, UINT, LCID, void **);
    HRESULT (STDMETHODCALLTYPE *GetIDsOfNames)(ITextDocument2 *, REFIID, LPOLESTR *, UINT, LCID, LONG *);
    HRESULT (STDMETHODCALLTYPE *Invoke)(ITextDocument2 *, LONG, REFIID, LCID, WORD, void *, VARIANT *, void *, UINT *);
    /* ITextDocument */
    HRESULT (STDMETHODCALLTYPE *GetName)(ITextDocument2 *, BSTR *);
    HRESULT (STDMETHODCALLTYPE *GetSelection)(ITextDocument2 *, ITextSelection **);
    HRESULT (STDMETHODCALLTYPE *GetStoryCount)(ITextDocument2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *GetStoryRanges)(ITextDocument2 *, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *GetSaved)(ITextDocument2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *SetSaved)(ITextDocument2 *, LONG);
    HRESULT (STDMETHODCALLTYPE *GetDefaultTabStop)(ITextDocument2 *, float *);
    HRESULT (STDMETHODCALLTYPE *SetDefaultTabStop)(ITextDocument2 *, float);
    HRESULT (STDMETHODCALLTYPE *New)(ITextDocument2 *);
    HRESULT (STDMETHODCALLTYPE *Open)(ITextDocument2 *, VARIANT *, LONG flags, LONG codepage);
    HRESULT (STDMETHODCALLTYPE *Save)(ITextDocument2 *, VARIANT *, LONG flags, LONG codepage);
    HRESULT (STDMETHODCALLTYPE *Freeze)(ITextDocument2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *Unfreeze)(ITextDocument2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *BeginEditCollection)(ITextDocument2 *);
    HRESULT (STDMETHODCALLTYPE *EndEditCollection)(ITextDocument2 *);
    HRESULT (STDMETHODCALLTYPE *Undo)(ITextDocument2 *, LONG count, LONG *done);
    HRESULT (STDMETHODCALLTYPE *Redo)(ITextDocument2 *, LONG count, LONG *done);
    HRESULT (STDMETHODCALLTYPE *Range)(ITextDocument2 *, LONG active, LONG anchor, ITextRange **);
    HRESULT (STDMETHODCALLTYPE *RangeFromPoint)(ITextDocument2 *, LONG x, LONG y, ITextRange **);
    /* ITextDocument2 */
    HRESULT (STDMETHODCALLTYPE *GetCaretType)(ITextDocument2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *SetCaretType)(ITextDocument2 *, LONG);
    HRESULT (STDMETHODCALLTYPE *GetDisplays)(ITextDocument2 *, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *GetDocumentFont)(ITextDocument2 *, void **);
    HRESULT (STDMETHODCALLTYPE *SetDocumentFont)(ITextDocument2 *, void *);
    HRESULT (STDMETHODCALLTYPE *GetDocumentPara)(ITextDocument2 *, void **);
    HRESULT (STDMETHODCALLTYPE *SetDocumentPara)(ITextDocument2 *, void *);
    HRESULT (STDMETHODCALLTYPE *GetEastAsianFlags)(ITextDocument2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *GetGenerator)(ITextDocument2 *, BSTR *);
    HRESULT (STDMETHODCALLTYPE *SetIMEInProgress)(ITextDocument2 *, LONG);
    HRESULT (STDMETHODCALLTYPE *GetNotificationMode)(ITextDocument2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *SetNotificationMode)(ITextDocument2 *, LONG);
    HRESULT (STDMETHODCALLTYPE *GetSelection2)(ITextDocument2 *, ITextSelection2 **);
    HRESULT (STDMETHODCALLTYPE *GetStoryRanges2)(ITextDocument2 *, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *GetTypographyOptions)(ITextDocument2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *GetVersion)(ITextDocument2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *GetWindow)(ITextDocument2 *, LONGLONG *);
    HRESULT (STDMETHODCALLTYPE *AttachMsgFilter)(ITextDocument2 *, IUnknown *);
    HRESULT (STDMETHODCALLTYPE *CheckTextLimit)(ITextDocument2 *, LONG cch, LONG *exceed);
    HRESULT (STDMETHODCALLTYPE *GetCallManager)(ITextDocument2 *, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *GetClientRect)(ITextDocument2 *, LONG type, LONG *left, LONG *top, LONG *right, LONG *bottom);
    HRESULT (STDMETHODCALLTYPE *GetEffectColor)(ITextDocument2 *, LONG index, LONG *value);
    HRESULT (STDMETHODCALLTYPE *GetImmContext)(ITextDocument2 *, LONGLONG *);
    HRESULT (STDMETHODCALLTYPE *GetPreferredFont)(ITextDocument2 *, LONG cp, LONG charrep, LONG options, LONG curcharrep,
                                                  LONG cursize, BSTR *name, LONG *pitch, LONG *size);
    HRESULT (STDMETHODCALLTYPE *GetProperty)(ITextDocument2 *, LONG type, LONG *value);
    HRESULT (STDMETHODCALLTYPE *GetStrings)(ITextDocument2 *, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *Notify)(ITextDocument2 *, LONG notify);
    HRESULT (STDMETHODCALLTYPE *Range2)(ITextDocument2 *, LONG active, LONG anchor, ITextRange2 **);
    HRESULT (STDMETHODCALLTYPE *RangeFromPoint2)(ITextDocument2 *, LONG x, LONG y, LONG type, ITextRange2 **);
    HRESULT (STDMETHODCALLTYPE *ReleaseCallManager)(ITextDocument2 *, IUnknown *);
    HRESULT (STDMETHODCALLTYPE *ReleaseImmContext)(ITextDocument2 *, LONGLONG);
    HRESULT (STDMETHODCALLTYPE *SetEffectColor)(ITextDocument2 *, LONG index, LONG value);
    HRESULT (STDMETHODCALLTYPE *SetProperty)(ITextDocument2 *, LONG type, LONG value);
    HRESULT (STDMETHODCALLTYPE *SetTypographyOptions)(ITextDocument2 *, LONG options, LONG mask);
    HRESULT (STDMETHODCALLTYPE *SysBeep)(ITextDocument2 *);
    HRESULT (STDMETHODCALLTYPE *Update)(ITextDocument2 *, LONG value);
    HRESULT (STDMETHODCALLTYPE *UpdateWindow)(ITextDocument2 *);
    HRESULT (STDMETHODCALLTYPE *GetMathProperties)(ITextDocument2 *, LONG *);
    HRESULT (STDMETHODCALLTYPE *SetMathProperties)(ITextDocument2 *, LONG options, LONG mask);
    HRESULT (STDMETHODCALLTYPE *GetActiveStory)(ITextDocument2 *, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *SetActiveStory)(ITextDocument2 *, IUnknown *);
    HRESULT (STDMETHODCALLTYPE *GetMainStory)(ITextDocument2 *, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *GetNewStory)(ITextDocument2 *, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *GetStory)(ITextDocument2 *, LONG index, IUnknown **);
} ITextDocument2Vtbl;
struct ITextDocument2 { const ITextDocument2Vtbl *lpVtbl; };

/* IRichEditOle (EM_GETOLEINTERFACE): no embedded objects here; its
 * QueryInterface gives the ITextDocument2 */
typedef struct IRichEditOle_Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IRichEditOle_ *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IRichEditOle_ *);
    ULONG (STDMETHODCALLTYPE *Release)(IRichEditOle_ *);
    HRESULT (STDMETHODCALLTYPE *GetClientSite)(IRichEditOle_ *, IUnknown **);
    LONG (STDMETHODCALLTYPE *GetObjectCount)(IRichEditOle_ *);
    LONG (STDMETHODCALLTYPE *GetLinkCount)(IRichEditOle_ *);
    HRESULT (STDMETHODCALLTYPE *GetObject)(IRichEditOle_ *, LONG iob, void *reobject, DWORD flags);
    HRESULT (STDMETHODCALLTYPE *InsertObject)(IRichEditOle_ *, void *reobject);
    HRESULT (STDMETHODCALLTYPE *ConvertObject)(IRichEditOle_ *, LONG iob, REFCLSID, LPCSTR);
    HRESULT (STDMETHODCALLTYPE *ActivateAs)(IRichEditOle_ *, REFCLSID, REFCLSID);
    HRESULT (STDMETHODCALLTYPE *SetHostNames)(IRichEditOle_ *, LPCSTR, LPCSTR);
    HRESULT (STDMETHODCALLTYPE *SetLinkAvailable)(IRichEditOle_ *, LONG iob, BOOL);
    HRESULT (STDMETHODCALLTYPE *SetDvaspect)(IRichEditOle_ *, LONG iob, DWORD);
    HRESULT (STDMETHODCALLTYPE *HandsOffStorage)(IRichEditOle_ *, LONG iob);
    HRESULT (STDMETHODCALLTYPE *SaveCompleted)(IRichEditOle_ *, LONG iob, IUnknown *storage);
    HRESULT (STDMETHODCALLTYPE *InPlaceDeactivate)(IRichEditOle_ *);
    HRESULT (STDMETHODCALLTYPE *ContextSensitiveHelp)(IRichEditOle_ *, BOOL);
    HRESULT (STDMETHODCALLTYPE *GetClipboardData)(IRichEditOle_ *, CHARRANGE *, DWORD, IUnknown **);
    HRESULT (STDMETHODCALLTYPE *ImportDataObject)(IRichEditOle_ *, IUnknown *, WORD cf, HGLOBAL);
} IRichEditOle_Vtbl;
struct IRichEditOle_ { const IRichEditOle_Vtbl *lpVtbl; };

#define NOVA_IID_IDispatch       {0x00020400, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}}
#define NOVA_IID_ITextDocument   {0x8cc497c0, 0xa1df, 0x11ce, {0x80, 0x98, 0x00, 0xaa, 0x00, 0x47, 0xbe, 0x5d}}
#define NOVA_IID_ITextSelection  {0x8cc497c1, 0xa1df, 0x11ce, {0x80, 0x98, 0x00, 0xaa, 0x00, 0x47, 0xbe, 0x5d}}
#define NOVA_IID_ITextRange      {0x8cc497c2, 0xa1df, 0x11ce, {0x80, 0x98, 0x00, 0xaa, 0x00, 0x47, 0xbe, 0x5d}}
#define NOVA_IID_ITextSelection2 {0xc241f5e1, 0x7206, 0x11d8, {0xa2, 0xc7, 0x00, 0xa0, 0xd1, 0xd6, 0xc6, 0xb3}}
#define NOVA_IID_ITextRange2     {0xc241f5e2, 0x7206, 0x11d8, {0xa2, 0xc7, 0x00, 0xa0, 0xd1, 0xd6, 0xc6, 0xb3}}

/* the slots of Direct2D's ID2D1RenderTarget that TxDrawD2D uses */
#define NOVA_D2D_CREATEBITMAP      4
#define NOVA_D2D_DRAWBITMAP        26
#define NOVA_D2D_GETDPI            52

/* the IIDs (riched20.dll, msftedit.dll and WinUIEdit.dll export them as data) */
#define NOVA_IID_ITextServices   {0x8d33f740, 0xcf58, 0x11ce, {0xa8, 0x9d, 0x00, 0xaa, 0x00, 0x6c, 0xad, 0xc5}}
#define NOVA_IID_ITextServices2  {0x8d33f741, 0xcf58, 0x11ce, {0xa8, 0x9d, 0x00, 0xaa, 0x00, 0x6c, 0xad, 0xc5}}
#define NOVA_IID_ITextHost       {0x13e670f4, 0x1a5a, 0x11cf, {0xab, 0xeb, 0x00, 0xaa, 0x00, 0xb6, 0x5e, 0xa1}}
#define NOVA_IID_ITextHost2      {0x13e670f5, 0x1a5a, 0x11cf, {0xab, 0xeb, 0x00, 0xaa, 0x00, 0xb6, 0x5e, 0xa1}}
#define NOVA_IID_ITextDocument2  {0xc241f5e0, 0x7206, 0x11d8, {0xa2, 0xc7, 0x00, 0xa0, 0xd1, 0xd6, 0xc6, 0xb3}}
#define NOVA_IID_IRicheditWindowlessAccessibility {0x983e572d, 0x20cd, 0x460b, {0x91, 0x04, 0x83, 0x11, 0x15, 0x92, 0xdd, 0x10}}
#define NOVA_IID_IRichEditOle    {0x00020d00, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}}
/* IProvideFontInfo: a host's DirectWrite font faces (XAML's text boxes'
 * host answers it; the IID is NovaOS's, as the interface's header is closed:
 * userland/windows_ui_xaml/inc/fontinfo.h) */
#define NOVA_IID_IProvideFontInfo {0xd81d4036, 0xc20c, 0x4297, {0x8c, 0xf1, 0x58, 0x35, 0xa6, 0x76, 0x61, 0x89}}
#define NOVA_IID_IRichEditOleCallback {0x00020d03, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}}
/* IRichEditOleCallback, the host's callback EM_SETOLECALLBACK gives: the
 * text services ask it for a context menu (GetContextMenu, the slot after
 * GetDragDropEffect) when the right button is let go or the menu key is;
 * XAML's text boxes show theirs from it */
typedef struct IRichEditOleCallback_ IRichEditOleCallback_;
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IRichEditOleCallback_ *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IRichEditOleCallback_ *);
    ULONG (STDMETHODCALLTYPE *Release)(IRichEditOleCallback_ *);
    HRESULT (STDMETHODCALLTYPE *GetNewStorage)(IRichEditOleCallback_ *, void **);
    HRESULT (STDMETHODCALLTYPE *GetInPlaceContext)(IRichEditOleCallback_ *, void **, void **, void *);
    HRESULT (STDMETHODCALLTYPE *ShowContainerUI)(IRichEditOleCallback_ *, BOOL);
    HRESULT (STDMETHODCALLTYPE *QueryInsertObject)(IRichEditOleCallback_ *, CLSID *, void *, LONG);
    HRESULT (STDMETHODCALLTYPE *DeleteObject)(IRichEditOleCallback_ *, void *);
    HRESULT (STDMETHODCALLTYPE *QueryAcceptData)(IRichEditOleCallback_ *, void *, UINT *, DWORD, BOOL, HGLOBAL);
    HRESULT (STDMETHODCALLTYPE *ContextSensitiveHelp)(IRichEditOleCallback_ *, BOOL);
    HRESULT (STDMETHODCALLTYPE *GetClipboardData)(IRichEditOleCallback_ *, CHARRANGE *, DWORD, void **);
    HRESULT (STDMETHODCALLTYPE *GetDragDropEffect)(IRichEditOleCallback_ *, BOOL, DWORD, DWORD *);
    HRESULT (STDMETHODCALLTYPE *GetContextMenu)(IRichEditOleCallback_ *, WORD seltype, void *oleobj, CHARRANGE *chrg,
                                                HMENU *menu);
} IRichEditOleCallback_Vtbl;
struct IRichEditOleCallback_ { const IRichEditOleCallback_Vtbl *lpVtbl; };
/* what GetContextMenu's @chrg is when @seltype has GCM_MOUSEMENU or
 * GCM_TOUCHMENU (Rich Edit 8's GETCONTEXTMENUEX): the point is the
 * pointer's, in the view's coordinates */
typedef struct { CHARRANGE chrg; DWORD dwFlags; POINT pt; void *pvReserved; void *pSpellingSuggestions;
                 WCHAR *bstrAutoCorrectedWord; } NOVA_GETCONTEXTMENUEX;
#define GCM_TOUCHMENU       0x4000
#define GCM_MOUSEMENU       0x2000
#define EM_SETTOUCHOPTIONS  (WM_USER + 311)
#define EM_GETTOUCHOPTIONS  (WM_USER + 312)
#define RTO_DISABLECONTEXTMENU 8                  /* (NovaOS's value, as WinUIEdit.h's) */

typedef HRESULT (WINAPI *PCREATETEXTSERVICES)(IUnknown *outer, ITextHost *host, IUnknown **out);
typedef HRESULT (WINAPI *PSHUTDOWNTEXTSERVICES)(IUnknown *services);
