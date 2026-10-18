/* commdlg.h — the print dialogs NovaOS's comdlg32.dll implements */
#pragma once
#include <windows.h>
_NOVA_BEGIN

/* 32-bit Windows packs these structures to 1 byte (PRINTDLG is 66 bytes there) */
#ifndef _WIN64
#pragma pack(push, 1)
#endif

typedef UINT_PTR (CALLBACK *LPPRINTHOOKPROC)(HWND, UINT, WPARAM, LPARAM);
typedef UINT_PTR (CALLBACK *LPSETUPHOOKPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct {
    DWORD lStructSize;
    HWND hwndOwner;
    HGLOBAL hDevMode, hDevNames;
    HDC hDC;
    DWORD Flags;
    WORD nFromPage, nToPage, nMinPage, nMaxPage, nCopies;
    HINSTANCE hInstance;
    LPARAM lCustData;
    LPPRINTHOOKPROC lpfnPrintHook;
    LPSETUPHOOKPROC lpfnSetupHook;
    LPCWSTR lpPrintTemplateName, lpSetupTemplateName;
    HGLOBAL hPrintTemplate, hSetupTemplate;
} PRINTDLGW, *LPPRINTDLGW;
typedef struct {
    DWORD lStructSize;
    HWND hwndOwner;
    HGLOBAL hDevMode, hDevNames;
    HDC hDC;
    DWORD Flags;
    WORD nFromPage, nToPage, nMinPage, nMaxPage, nCopies;
    HINSTANCE hInstance;
    LPARAM lCustData;
    LPPRINTHOOKPROC lpfnPrintHook;
    LPSETUPHOOKPROC lpfnSetupHook;
    LPCSTR lpPrintTemplateName, lpSetupTemplateName;
    HGLOBAL hPrintTemplate, hSetupTemplate;
} PRINTDLGA, *LPPRINTDLGA;

typedef struct { DWORD nFromPage, nToPage; } PRINTPAGERANGE, *LPPRINTPAGERANGE;
typedef struct {
    DWORD lStructSize;
    HWND hwndOwner;
    HGLOBAL hDevMode, hDevNames;
    HDC hDC;
    DWORD Flags, Flags2, ExclusionFlags, nPageRanges, nMaxPageRanges;
    LPPRINTPAGERANGE lpPageRanges;
    DWORD nMinPage, nMaxPage, nCopies;
    HINSTANCE hInstance;
    LPCWSTR lpPrintTemplateName;
    void *lpCallback;
    DWORD nPropertyPages;
    void *lphPropertyPages;
    DWORD nStartPage, dwResultAction;
} PRINTDLGEXW, *LPPRINTDLGEXW;

typedef struct { WORD wDriverOffset, wDeviceOffset, wOutputOffset, wDefault; } DEVNAMES, *LPDEVNAMES;
#define DN_DEFAULTPRN 0x0001

typedef struct {
    DWORD lStructSize;
    HWND hwndOwner;
    HGLOBAL hDevMode, hDevNames;
    DWORD Flags;
    POINT ptPaperSize;
    RECT rtMinMargin, rtMargin;
    HINSTANCE hInstance;
    LPARAM lCustData;
    void *lpfnPageSetupHook, *lpfnPagePaintHook;
    LPCWSTR lpPageSetupTemplateName;
    HGLOBAL hPageSetupTemplate;
} PAGESETUPDLGW, *LPPAGESETUPDLGW;

#define PD_ALLPAGES            0x00000000
#define PD_SELECTION           0x00000001
#define PD_PAGENUMS            0x00000002
#define PD_NOSELECTION         0x00000004
#define PD_NOPAGENUMS          0x00000008
#define PD_COLLATE             0x00000010
#define PD_PRINTTOFILE         0x00000020
#define PD_PRINTSETUP          0x00000040
#define PD_NOWARNING           0x00000080
#define PD_RETURNDC            0x00000100
#define PD_RETURNIC            0x00000200
#define PD_RETURNDEFAULT       0x00000400
#define PD_SHOWHELP            0x00000800
#define PD_ENABLEPRINTHOOK     0x00001000
#define PD_ENABLESETUPHOOK     0x00002000
#define PD_USEDEVMODECOPIES    0x00040000
#define PD_USEDEVMODECOPIESANDCOLLATE 0x00040000
#define PD_DISABLEPRINTTOFILE  0x00080000
#define PD_HIDEPRINTTOFILE     0x00100000
#define PD_NONETWORKBUTTON     0x00200000
#define PD_CURRENTPAGE         0x00400000
#define PD_NOCURRENTPAGE       0x00800000
#define PD_EXCLUSIONFLAGS      0x01000000
#define PD_USELARGETEMPLATE    0x10000000

#define PD_RESULT_CANCEL 0
#define PD_RESULT_PRINT  1
#define PD_RESULT_APPLY  2
#define START_PAGE_GENERAL 0xFFFFFFFF

#define PSD_MARGINS            0x00000002
#define PSD_INTHOUSANDTHSOFINCHES 0x00000004
#define PSD_INHUNDREDTHSOFMILLIMETERS 0x00000008
#define PSD_RETURNDEFAULT      0x00000400

#define CDERR_STRUCTSIZE     0x0001
#define CDERR_INITIALIZATION 0x0002
#define PDERR_SETUPFAILURE   0x1001
#define PDERR_RETDEFFAILURE  0x1003
#define PDERR_PRINTERNOTFOUND 0x100B
#define PDERR_NODEFAULTPRN   0x1008
#define PDERR_CREATEICFAILURE 0x100A

#ifndef _WIN64
#pragma pack(pop)
#endif

#ifndef CDAPI_
#define CDAPI_ __declspec(dllimport) __stdcall
#endif
BOOL CDAPI_ PrintDlgW(LPPRINTDLGW pd);
BOOL CDAPI_ PrintDlgA(LPPRINTDLGA pd);
HRESULT CDAPI_ PrintDlgExW(LPPRINTDLGEXW pd);
BOOL CDAPI_ PageSetupDlgW(LPPAGESETUPDLGW psd);
DWORD CDAPI_ CommDlgExtendedError(void);

_NOVA_END
