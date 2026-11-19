// NovaOS: WinUIEdit.h, the Windows App SDK's (closed) header for its own
// copy of the windowless RichEdit (ITextServices2, ITextHost2 and the Text
// Object Model) that XAML's text boxes host.  Here the Windows SDK's
// RichEdit, text services and TOM headers (MinGW-w64's, with the second
// versions of the text services, host and TOM interfaces NovaOS adds in
// inc/textserv.h and inc/tom.h) and the interface they lack: Rich Edit's
// UI Automation provider factory.  NovaOS's WinUIEdit.dll is
// userland/textserv (userland/winuiedit).
#pragma once
#include <richedit.h>
#include <richole.h>
#include <textserv.h>
#include <tom.h>

/* WinUIEdit's own messages and flags (NovaOS's values: its WinUIEdit.dll
 * answers each as Rich Edit answers a message it does not keep, with no
 * result: no image cache, no touch selection grippers) */
#define EM_CLEARIMAGECACHE          (WM_USER + 0x170)
#define EM_VISIBLERECTCHANGED       (WM_USER + 0x171)
#define EM_GETSELECTIONEDGES        (WM_USER + 0x172)   /* lp: RECT *[2], the selection's ends */
#define EM_GETRECTSOFRANGE          (WM_USER + 0x173)   /* wp: GETRECTSOFRANGE * */
/* (the Text Services Framework 3 edit context, lp: ICoreTextEditContext **;
 * NovaOS has none and leaves it null, as XAML expects off the UI thread) */
#define EM_GETTSF                   (WM_USER + 0x174)
/* (pointer input from a XAML text box, lp: RE_MOUSEINPUT *; WinUIEdit.dll
 * answers it as the mouse message it carries, at its point) */
#define EM_MOUSEINPUT               (WM_USER + 0x175)
#define EM_ONCANVASTOUCHTAP         (WM_USER + 0x176)   /* wp: CANVASTOUCHPARAMS * */
#define EM_ONCANVASTOUCHHOLD        (WM_USER + 0x177)
#define EM_GETCOMPOSITIONRANGE      (WM_USER + 0x178)
#define EM_GETFREEZECOUNT           (WM_USER + 0x179)
#define EM_GETSPELLINGINFOFORCP     (WM_USER + 0x17a)   /* lp: GETCONTEXTMENUEX * */
#define EM_RESETHWND                (WM_USER + 0x17b)
#define EM_SETCTFINPUTSCOPES        (WM_USER + 0x17c)
#define EM_SETDPI                   (WM_USER + 0x17d)
#define EM_SETSIPDISPLAYPOLICY      (WM_USER + 0x17e)
#define EM_SETTOUCHSCALE            (WM_USER + 0x17f)
#define EM_TERMINATECOMPOSITION     (WM_USER + 0x180)
/* (a RichEditBox's: the formatting keys it allows, lp: CFE_BOLD,
 * CFE_ITALIC, CFE_UNDERLINE; and a link's navigation asked for by UI
 * Automation, which XAML handles as a click on it) */
#define EM_SETFORMATACCELERATORS    (WM_USER + 0x181)
#define EM_HANDLELINKNOTIFICATION   (WM_USER + 0x182)
/* (the host's answer to EN_LINK: let the Rich Edit do what it does) */
#define EN_LINK_DO_DEFAULT          S_FALSE
#ifndef IMF_COPYPLAINTEXTONLY
#define IMF_COPYPLAINTEXTONLY       0x00008000            /* (a RichEditBox's ClipboardCopyFormat PlainText) */
#endif
/* (notifications to the host: a paste, copy or cut the user asked for,
 * and UI Automation's select) */
#define EN_PASTEREQUESTED           0x0730
#define EN_COPYREQUESTED            0x0731
#define EN_CUTREQUESTED             0x0732
#define EN_UIASELECT                0x0733
#define GCMF_SPELLING_REPEATEDWORD  0x00000004
#define GCMF_SPELLING_AUTOCORRECTED 0x00000008
#ifndef IMF_IMEUIINTEGRATION
#define IMF_IMEUIINTEGRATION        0x2000
#endif
#define RTO_DISABLECONTEXTMENU      8
struct RE_MOUSEINPUT {               /* (userland/include/novatextserv.h: the same; a struct
                                       * name, as XAML's TextBoxBase.h declares it) */
    UINT msg;
    WPARAM wparam;
    POINT pt;                       /* in the text box view */
    POINT screenPtInRawPixels;
    BOOL isPenInput;
};
struct CANVASTOUCHPARAMS {
    POINT ptTouch;
    int tapCnt;
    BOOL fIsLive;
    INT32 *pcpResult;
};
#define RTO_SHOWHANDLES_IGNOREDRAG  4
#ifndef CFU_UNDERLINEIMEWAVE
#define CFU_UNDERLINEIMEWAVE        0x19
#endif
#ifndef CARET_RTL
#define CARET_NONE                  0x00
#define CARET_CUSTOM                0x01                /* (TxCreateCaret's bitmap, as flags) */
#define CARET_RTL                   0x02
#define CARET_ITALIC                0x20
#endif
typedef struct _getrectsofrange {
    CHARRANGE chrg;
    PVOID pvArg;
    BOOL (CALLBACK *pfnCallback)(PVOID pvArg, LPCRECT pcrect);
} GETRECTSOFRANGE;

#ifdef __cplusplus
/* the extensions WinUIEdit's Direct2D drawing asks its render target for
 * (XAML's HWRenderTarget answers them; the IID is NovaOS's) */
struct DECLSPEC_UUID("98f81090-b024-4cc7-99b2-6fa9ea993592") ID2DRenderTargetRichEditExtensions : public IUnknown {
    virtual bool DrawUnderline(BYTE underlineType, DWORD color, const RECT *fillrect) = 0;
};

struct IRawElementProviderWindowlessSite;
struct IRawElementProviderSimple;
struct DECLSPEC_UUID("983e572d-20cd-460b-9104-83111592dd10") IRicheditWindowlessAccessibility : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateProvider(IRawElementProviderWindowlessSite *pSite,
                                                     IRawElementProviderSimple **ppProvider) = 0;
};
#endif
