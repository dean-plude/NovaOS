/*
 * printdlg.c — the Print, Print Setup and Page Setup dialogs (PrintDlg,
 * PrintDlgEx, PageSetupDlg)
 *
 * Print: the printer (a list of winspool's printers, the default chosen,
 * with its status, type and port), print to file, the page range (all,
 * selection, pages from-to) and the copies (with collate).  Print Setup
 * (PD_PRINTSETUP) has the printer, the paper and the orientation instead.
 * The choice comes back as Windows returns it: a DEVMODE and a DEVNAMES
 * in movable global memory, and with PD_RETURNDC a printer DC (gdi32's
 * print.c).  PD_RETURNDEFAULT answers for the default printer without a
 * dialog.  The controls keep Windows' IDs (cmb4, rad1, edt1...), which
 * automation and programs' hook procedures look for.
 */

#include <windows.h>
#define CDAPI_ __declspec(dllexport) __stdcall
#include <commdlg.h>
#include <winspool.h>
#include <string.h>
#include "filedlg.h"

#define CDAPI __declspec(dllexport)

/* Windows' control IDs (dlgs.h) */
enum { chx1 = 0x410, chx2 = 0x411, grp1 = 0x430, grp2 = 0x431, grp3 = 0x432, grp4 = 0x433,
       rad1 = 0x420, rad2 = 0x421, rad3 = 0x422, stc1 = 0x440, stc2 = 0x441, stc3 = 0x442, stc4 = 0x443,
       stc5 = 0x444, stc6 = 0x445, stc7 = 0x446, stc8 = 0x447, stc11 = 0x44A, stc12 = 0x44B, stc13 = 0x44C,
       stc14 = 0x44D, stc15 = 0x44E, cmb1 = 0x470, cmb2 = 0x471, cmb4 = 0x473, edt1 = 0x480, edt2 = 0x481,
       edt3 = 0x482, edt4 = 0x483, edt5 = 0x484, edt6 = 0x485, psh2 = 0x401 };

#define MAX_PR 64

typedef struct {
    BOOL setup;                         /* Print Setup (PD_PRINTSETUP) */
    BOOL page_setup;                    /* Page Setup (PageSetupDlg) */
    DWORD flags;
    WCHAR printer[128];
    DEVMODEW dm;
    WORD from, to, min, max, copies;
    RECT margins;                       /* Page Setup: the margins, in the caller's units */
    BOOL inches;                        /* Page Setup: thousandths of an inch (else hundredths of a millimetre) */
    WCHAR names[MAX_PR][128];
    int nnames;
    HWND hwnd;
} PrintDlgState;

static int wlen(const WCHAR *s) { int n = 0; if (s) while (s[n]) n++; return n; }
static void wcopy(WCHAR *d, const WCHAR *s, int cap)
{
    int i = 0;
    for (; s && s[i] && i < cap - 1; i++) d[i] = s[i];
    d[i] = 0;
}

/* -----------------------------------------------------------------------
 * The printers
 * ----------------------------------------------------------------------- */
static int printers(WCHAR (*names)[128], int cap)
{
    DWORD need = 0, n = 0;
    EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 4, NULL, 0, &need, &n);
    if (!need) return 0;
    BYTE *buf = HeapAlloc(GetProcessHeap(), 0, need);
    int m = 0;
    if (buf && EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 4, buf, need, &need, &n))
        for (DWORD i = 0; i < n && m < cap; i++) wcopy(names[m++], ((PRINTER_INFO_4W *)buf)[i].pPrinterName, 128);
    HeapFree(GetProcessHeap(), 0, buf);
    return m;
}

/* The printer's DEVMODE, with what @in says over its defaults */
static BOOL printer_devmode(const WCHAR *name, const DEVMODEW *in, DEVMODEW *out)
{
    return DocumentPropertiesW(NULL, NULL, (LPWSTR)name, out, (DEVMODEW *)in, DM_OUT_BUFFER | (in ? DM_IN_BUFFER : 0)) == IDOK;
}

static void printer_port(const WCHAR *name, WCHAR *port, WCHAR *driver, WCHAR *status, WCHAR *comment)
{
    HANDLE h;
    port[0] = driver[0] = comment[0] = 0;
    wcopy(status, L"Ready", 64);
    if (!OpenPrinterW((LPWSTR)name, &h, NULL)) return;
    DWORD need = 0;
    GetPrinterW(h, 2, NULL, 0, &need);
    BYTE *buf = need ? HeapAlloc(GetProcessHeap(), 0, need) : NULL;
    if (buf && GetPrinterW(h, 2, buf, need, &need)) {
        PRINTER_INFO_2W *pi = (PRINTER_INFO_2W *)buf;
        wcopy(port, pi->pPortName, MAX_PATH);
        wcopy(driver, pi->pDriverName, 128);
        wcopy(comment, pi->pComment, 128);
        if (pi->Status & PRINTER_STATUS_PAUSED) wcopy(status, L"Paused", 64);
        else if (pi->cJobs) {
            WCHAR n[16];
            int i = 15;
            DWORD v = pi->cJobs;
            n[i] = 0;
            do { n[--i] = (WCHAR)('0' + v % 10); v /= 10; } while (v);
            wcopy(status, n + i, 64);
            wcopy(status + wlen(status), pi->cJobs == 1 ? L" document waiting" : L" documents waiting", 64 - wlen(status));
        }
    }
    HeapFree(GetProcessHeap(), 0, buf);
    ClosePrinter(h);
}

/* DEVMODE and DEVNAMES in movable global memory, as programs free them */
static HGLOBAL global_devmode(const DEVMODEW *dm)
{
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof *dm);
    void *p = g ? GlobalLock(g) : NULL;
    if (p) { memcpy(p, dm, sizeof *dm); GlobalUnlock(g); }
    return g;
}

static HGLOBAL global_devnames(const WCHAR *printer, const WCHAR *port, BOOL dflt)
{
    static const WCHAR drv[] = L"winspool";
    int n1 = wlen(drv) + 1, n2 = wlen(printer) + 1, n3 = wlen(port) + 1;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DEVNAMES) + (n1 + n2 + n3) * sizeof(WCHAR));
    DEVNAMES *dn = g ? GlobalLock(g) : NULL;
    if (!dn) return g;
    WCHAR *s = (WCHAR *)(dn + 1);
    dn->wDriverOffset = sizeof(DEVNAMES) / sizeof(WCHAR);
    dn->wDeviceOffset = (WORD)(dn->wDriverOffset + n1);
    dn->wOutputOffset = (WORD)(dn->wDeviceOffset + n2);
    dn->wDefault = dflt ? DN_DEFAULTPRN : 0;
    memcpy(s, drv, n1 * sizeof(WCHAR));
    memcpy(s + n1, printer, n2 * sizeof(WCHAR));
    memcpy(s + n1 + n2, port, n3 * sizeof(WCHAR));
    GlobalUnlock(g);
    return g;
}

/* The printer a caller's DEVNAMES or DEVMODE names (else the default) */
static BOOL chosen_printer(HGLOBAL hnames, HGLOBAL hmode, WCHAR *out)
{
    out[0] = 0;
    DEVNAMES *dn = hnames ? GlobalLock(hnames) : NULL;
    if (dn) { wcopy(out, (WCHAR *)dn + dn->wDeviceOffset, 128); GlobalUnlock(hnames); }
    if (!out[0]) {
        DEVMODEW *dm = hmode ? GlobalLock(hmode) : NULL;
        if (dm) { wcopy(out, dm->dmDeviceName, 32); GlobalUnlock(hmode); }
    }
    HANDLE h;
    if (out[0] && OpenPrinterW(out, &h, NULL)) { ClosePrinter(h); return TRUE; }
    DWORD n = 128;
    return GetDefaultPrinterW(out, &n);
}

static BOOL is_default_printer(const WCHAR *name)
{
    WCHAR d[128];
    DWORD n = 128;
    return GetDefaultPrinterW(d, &n) && !lstrcmpiW(d, name);
}

/* -----------------------------------------------------------------------
 * The dialog
 * ----------------------------------------------------------------------- */
static const struct { short id; const WCHAR *name; } g_papers[] = {
    { DMPAPER_LETTER, L"Letter" }, { DMPAPER_LEGAL, L"Legal" }, { DMPAPER_A3, L"A3" }, { DMPAPER_A4, L"A4" }, { DMPAPER_A5, L"A5" },
};
#define NPAPERS (int)(sizeof g_papers / sizeof g_papers[0])

static void show_printer(PrintDlgState *s)
{
    WCHAR port[MAX_PATH], driver[128], status[64], comment[128];
    printer_port(s->printer, port, driver, status, comment);
    SetDlgItemTextW(s->hwnd, stc12, status);
    SetDlgItemTextW(s->hwnd, stc11, driver);
    SetDlgItemTextW(s->hwnd, stc14, port);
    SetDlgItemTextW(s->hwnd, stc13, comment);
}

static void init(PrintDlgState *s)
{
    HWND h = s->hwnd;
    s->nnames = printers(s->names, MAX_PR);
    int sel = 0;
    for (int i = 0; i < s->nnames; i++) {
        SendDlgItemMessageW(h, cmb4, CB_ADDSTRING, 0, (LPARAM)s->names[i]);
        if (!lstrcmpiW(s->names[i], s->printer)) sel = i;
    }
    SendDlgItemMessageW(h, cmb4, CB_SETCURSEL, sel, 0);
    show_printer(s);
    if (s->setup || s->page_setup) {
        int psel = 0;
        for (int i = 0; i < NPAPERS; i++) {
            SendDlgItemMessageW(h, cmb2, CB_ADDSTRING, 0, (LPARAM)g_papers[i].name);
            if (g_papers[i].id == s->dm.dmPaperSize) psel = i;
        }
        SendDlgItemMessageW(h, cmb2, CB_SETCURSEL, psel, 0);
        CheckRadioButton(h, rad1, rad2, s->dm.dmOrientation == DMORIENT_LANDSCAPE ? rad2 : rad1);
        if (s->page_setup) {
            SetDlgItemInt(h, edt4, (UINT)s->margins.left, FALSE);
            SetDlgItemInt(h, edt5, (UINT)s->margins.top, FALSE);
            SetDlgItemInt(h, edt6, (UINT)s->margins.right, FALSE);
            SetDlgItemInt(h, edt3, (UINT)s->margins.bottom, FALSE);
        }
        return;
    }
    CheckDlgButton(h, chx1, (s->flags & PD_PRINTTOFILE) ? BST_CHECKED : BST_UNCHECKED);
    if (s->flags & PD_DISABLEPRINTTOFILE) EnableWindow(GetDlgItem(h, chx1), FALSE);
    if (s->flags & PD_HIDEPRINTTOFILE) ShowWindow(GetDlgItem(h, chx1), SW_HIDE);
    int range = (s->flags & PD_PAGENUMS) ? rad3 : (s->flags & PD_SELECTION) ? rad2 : rad1;
    CheckRadioButton(h, rad1, rad3, range);
    if (s->flags & PD_NOSELECTION) EnableWindow(GetDlgItem(h, rad2), FALSE);
    if (s->flags & PD_NOPAGENUMS) {
        EnableWindow(GetDlgItem(h, rad3), FALSE);
        EnableWindow(GetDlgItem(h, edt1), FALSE);
        EnableWindow(GetDlgItem(h, edt2), FALSE);
    }
    SetDlgItemInt(h, edt1, s->from ? s->from : s->min ? s->min : 1, FALSE);
    SetDlgItemInt(h, edt2, s->to ? s->to : s->max && s->max != 0xFFFF ? s->max : 1, FALSE);
    SetDlgItemInt(h, edt3, s->copies ? s->copies : 1, FALSE);
    CheckDlgButton(h, chx2, (s->flags & PD_COLLATE) ? BST_CHECKED : BST_UNCHECKED);
}

/* OK: what the dialog shows into the state; FALSE (and a message) when a field is wrong */
static BOOL take(PrintDlgState *s)
{
    HWND h = s->hwnd;
    int sel = (int)SendDlgItemMessageW(h, cmb4, CB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < s->nnames && lstrcmpiW(s->names[sel], s->printer)) {
        DEVMODEW keep = s->dm;
        wcopy(s->printer, s->names[sel], 128);
        keep.dmFields &= DM_ORIENTATION | DM_PAPERSIZE | DM_COPIES | DM_COLLATE | DM_COLOR;
        printer_devmode(s->printer, &keep, &s->dm);
    }
    if (s->setup || s->page_setup) {
        int p = (int)SendDlgItemMessageW(h, cmb2, CB_GETCURSEL, 0, 0);
        if (p >= 0 && p < NPAPERS) {
            s->dm.dmPaperSize = g_papers[p].id;
            wcopy(s->dm.dmFormName, g_papers[p].name, 32);
        }
        s->dm.dmOrientation = IsDlgButtonChecked(h, rad2) ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
        if (s->page_setup) {
            BOOL ok1, ok2, ok3, ok4;
            RECT m = { (LONG)GetDlgItemInt(h, edt4, &ok1, FALSE), (LONG)GetDlgItemInt(h, edt5, &ok2, FALSE),
                       (LONG)GetDlgItemInt(h, edt6, &ok3, FALSE), (LONG)GetDlgItemInt(h, edt3, &ok4, FALSE) };
            if (ok1 && ok2 && ok3 && ok4) s->margins = m;
        }
        return TRUE;
    }
    s->flags &= ~(PD_PAGENUMS | PD_SELECTION | PD_PRINTTOFILE | PD_COLLATE);
    if (IsDlgButtonChecked(h, chx1)) s->flags |= PD_PRINTTOFILE;
    if (IsDlgButtonChecked(h, chx2)) s->flags |= PD_COLLATE;
    if (IsDlgButtonChecked(h, rad2)) s->flags |= PD_SELECTION;
    if (IsDlgButtonChecked(h, rad3)) {
        BOOL ok1, ok2;
        UINT f = GetDlgItemInt(h, edt1, &ok1, FALSE), t = GetDlgItemInt(h, edt2, &ok2, FALSE);
        if (!ok1 || !ok2 || f > t || (s->min && f < s->min) || (s->max && t > s->max)) {
            MessageBoxW(h, L"The page range is outside the document's pages.", L"Print", MB_OK | MB_ICONWARNING);
            SetFocus(GetDlgItem(h, edt1));
            return FALSE;
        }
        s->flags |= PD_PAGENUMS;
        s->from = (WORD)f;
        s->to = (WORD)t;
    }
    BOOL ok;
    UINT c = GetDlgItemInt(h, edt3, &ok, FALSE);
    if (!ok || c < 1 || c > 999) {
        MessageBoxW(h, L"Enter a number of copies from 1 to 999.", L"Print", MB_OK | MB_ICONWARNING);
        SetFocus(GetDlgItem(h, edt3));
        return FALSE;
    }
    s->copies = (WORD)c;
    return TRUE;
}

static INT_PTR CALLBACK dlg_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PrintDlgState *s = (PrintDlgState *)GetWindowLongPtrW(h, DWLP_USER);
    switch (msg) {
    case WM_INITDIALOG:
        s = (PrintDlgState *)lp;
        SetWindowLongPtrW(h, DWLP_USER, (LONG_PTR)s);
        s->hwnd = h;
        init(s);
        SetFocus(GetDlgItem(h, IDOK));
        return FALSE;
    case WM_COMMAND:
        if (!s) break;
        switch (LOWORD(wp)) {
        case IDOK: if (take(s)) EndDialog(h, IDOK); return TRUE;
        case IDCANCEL: EndDialog(h, IDCANCEL); return TRUE;
        case cmb4:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int sel = (int)SendDlgItemMessageW(h, cmb4, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < s->nnames) {
                    DEVMODEW keep = s->dm;
                    wcopy(s->printer, s->names[sel], 128);
                    keep.dmFields &= DM_ORIENTATION | DM_PAPERSIZE | DM_COPIES | DM_COLLATE | DM_COLOR;
                    printer_devmode(s->printer, &keep, &s->dm);
                    show_printer(s);
                }
            }
            return TRUE;
        case edt1: case edt2:                     /* typing a page number picks "Pages" */
            if (HIWORD(wp) == EN_CHANGE && GetFocus() == GetDlgItem(h, LOWORD(wp))) CheckRadioButton(h, rad1, rad3, rad3);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(h, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

/* an in-memory dialog template */
typedef struct { WORD *p; int n; } Tb;
static void t_word(Tb *t, WORD w) { *t->p++ = w; }
static void t_str(Tb *t, const WCHAR *s) { do t_word(t, *s); while (*s++); }
static void t_align(Tb *t) { if ((ULONG_PTR)t->p & 2) t_word(t, 0); }
static void t_item(Tb *t, DWORD style, short x, short y, short cx, short cy, WORD id, WORD atom, const WCHAR *text)
{
    t_align(t);
    DLGITEMTEMPLATE *it = (DLGITEMTEMPLATE *)t->p;
    it->style = style | WS_CHILD | WS_VISIBLE; it->dwExtendedStyle = 0;
    it->x = x; it->y = y; it->cx = cx; it->cy = cy; it->id = id;
    t->p = (WORD *)(it + 1);
    t_word(t, 0xFFFF); t_word(t, atom);
    t_str(t, text);
    t_word(t, 0);
    t->n++;
}
#define BTN 0x80
#define EDIT 0x81
#define STATIC 0x82
#define COMBO 0x85

static BOOL run(PrintDlgState *s, HWND owner)
{
    static DWORD buf[2048];
    Tb t = { (WORD *)buf, 0 };
    DLGTEMPLATE *dt = (DLGTEMPLATE *)t.p;
    dt->style = DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dt->dwExtendedStyle = 0;
    dt->x = dt->y = 0; dt->cx = 290;
    dt->cy = s->page_setup ? 200 : 176;
    t.p = (WORD *)(dt + 1);
    t_word(&t, 0); t_word(&t, 0);
    t_str(&t, s->page_setup ? L"Page Setup" : s->setup ? L"Print Setup" : L"Print");
    t_word(&t, 8); t_str(&t, L"MS Shell Dlg");
    short y0 = 0;
    if (s->page_setup) {                          /* the paper first, as Windows' Page Setup */
        t_item(&t, BS_GROUPBOX, 7, 4, 276, 46, grp2, BTN, L"Paper");
        t_item(&t, SS_LEFT, 14, 18, 40, 9, stc2, STATIC, L"Si&ze:");
        t_item(&t, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 56, 16, 220, 100, cmb2, COMBO, L"");
        t_item(&t, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 56, 33, 60, 10, rad1, BTN, L"P&ortrait");
        t_item(&t, BS_AUTORADIOBUTTON, 120, 33, 60, 10, rad2, BTN, L"L&andscape");
        y0 = 50;
    }
    t_item(&t, BS_GROUPBOX, 7, (short)(4 + y0), 276, 78, grp4, BTN, L"Printer");
    t_item(&t, SS_LEFT, 14, (short)(20 + y0), 40, 9, stc6, STATIC, L"&Name:");
    t_item(&t, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_GROUP, 56, (short)(18 + y0), 220, 100, cmb4, COMBO, L"");
    t_item(&t, SS_LEFT, 14, (short)(36 + y0), 40, 9, stc8, STATIC, L"Status:");
    t_item(&t, SS_LEFT | SS_NOPREFIX, 56, (short)(36 + y0), 220, 9, stc12, STATIC, L"");
    t_item(&t, SS_LEFT, 14, (short)(47 + y0), 40, 9, stc7, STATIC, L"Type:");
    t_item(&t, SS_LEFT | SS_NOPREFIX, 56, (short)(47 + y0), 220, 9, stc11, STATIC, L"");
    t_item(&t, SS_LEFT, 14, (short)(58 + y0), 40, 9, stc5, STATIC, L"Where:");
    t_item(&t, SS_LEFT | SS_NOPREFIX, 56, (short)(58 + y0), 160, 9, stc14, STATIC, L"");
    t_item(&t, SS_LEFT, 14, (short)(69 + y0), 40, 9, stc15, STATIC, L"Comment:");
    t_item(&t, SS_LEFT | SS_NOPREFIX, 56, (short)(69 + y0), 160, 9, stc13, STATIC, L"");
    if (s->page_setup) {
        t_item(&t, BS_GROUPBOX, 7, 136, 276, 40, grp4 + 1, BTN, L"Margins");
        t_item(&t, SS_LEFT, 14, 150, 30, 9, stc1, STATIC, L"&Left:");
        t_item(&t, ES_NUMBER | WS_BORDER | WS_TABSTOP, 46, 148, 40, 12, edt4, EDIT, L"");
        t_item(&t, SS_LEFT, 150, 150, 30, 9, stc3, STATIC, L"&Right:");
        t_item(&t, ES_NUMBER | WS_BORDER | WS_TABSTOP, 182, 148, 40, 12, edt6, EDIT, L"");
        t_item(&t, SS_LEFT, 14, 163, 30, 9, stc4, STATIC, L"&Top:");
        t_item(&t, ES_NUMBER | WS_BORDER | WS_TABSTOP, 46, 161, 40, 12, edt5, EDIT, L"");
        t_item(&t, SS_LEFT, 150, 163, 30, 9, stc2 + 0x10, STATIC, L"&Bottom:");
        t_item(&t, ES_NUMBER | WS_BORDER | WS_TABSTOP, 182, 161, 40, 12, edt3, EDIT, L"");
    } else if (s->setup) {
        t_item(&t, BS_GROUPBOX, 7, 86, 160, 44, grp2, BTN, L"Paper");
        t_item(&t, SS_LEFT, 14, 101, 30, 9, stc2, STATIC, L"Si&ze:");
        t_item(&t, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_GROUP, 46, 99, 114, 100, cmb2, COMBO, L"");
        t_item(&t, BS_GROUPBOX, 173, 86, 110, 44, grp1, BTN, L"Orientation");
        t_item(&t, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 181, 99, 90, 10, rad1, BTN, L"P&ortrait");
        t_item(&t, BS_AUTORADIOBUTTON, 181, 112, 90, 10, rad2, BTN, L"L&andscape");
    } else {
        t_item(&t, BS_AUTOCHECKBOX | WS_TABSTOP, 220, 66, 60, 10, chx1, BTN, L"Print to fi&le");
        t_item(&t, BS_GROUPBOX, 7, 86, 160, 64, grp1, BTN, L"Page range");
        t_item(&t, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 14, 99, 60, 10, rad1, BTN, L"&All");
        t_item(&t, BS_AUTORADIOBUTTON, 14, 113, 60, 10, rad2, BTN, L"S&election");
        t_item(&t, BS_AUTORADIOBUTTON, 14, 127, 40, 10, rad3, BTN, L"Pa&ges");
        t_item(&t, SS_LEFT, 56, 128, 20, 9, stc2, STATIC, L"&from:");
        t_item(&t, ES_NUMBER | WS_BORDER | WS_TABSTOP | WS_GROUP, 78, 126, 26, 12, edt1, EDIT, L"");
        t_item(&t, SS_LEFT, 108, 128, 12, 9, stc3, STATIC, L"&to:");
        t_item(&t, ES_NUMBER | WS_BORDER | WS_TABSTOP, 122, 126, 26, 12, edt2, EDIT, L"");
        t_item(&t, BS_GROUPBOX, 173, 86, 110, 64, grp2, BTN, L"Copies");
        t_item(&t, SS_LEFT, 181, 101, 56, 9, stc5 + 0x10, STATIC, L"Number of &copies:");
        t_item(&t, ES_NUMBER | WS_BORDER | WS_TABSTOP | WS_GROUP, 243, 99, 32, 12, edt3, EDIT, L"");
        t_item(&t, BS_AUTOCHECKBOX | WS_TABSTOP, 181, 120, 60, 10, chx2, BTN, L"C&ollate");
    }
    short by = (short)(dt->cy - 20);
    t_item(&t, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 172, by, 52, 14, IDOK, BTN, s->setup || s->page_setup ? L"OK" : L"&Print");
    t_item(&t, BS_PUSHBUTTON | WS_TABSTOP, 231, by, 52, 14, IDCANCEL, BTN, L"Cancel");
    dt->cdit = (WORD)t.n;
    s->hwnd = 0;
    HMODULE self = GetModuleHandleW(L"comdlg32.dll");
    return DialogBoxIndirectParamW(self, (LPCDLGTEMPLATEW)buf, owner, dlg_proc, (LPARAM)s) == IDOK;
}

/* -----------------------------------------------------------------------
 * PrintDlg
 * ----------------------------------------------------------------------- */
static void replace_global(HGLOBAL *slot, HGLOBAL g)
{
    if (*slot) GlobalFree(*slot);
    *slot = g;
}

/* Fill the caller's DEVMODE/DEVNAMES/DC from the state; FALSE when the DC cannot be made */
static BOOL hand_back(PrintDlgState *s, HGLOBAL *hmode, HGLOBAL *hnames, HDC *hdc, DWORD flags)
{
    WCHAR port[MAX_PATH], driver[128], status[64], comment[128];
    printer_port(s->printer, port, driver, status, comment);
    if (s->flags & PD_PRINTTOFILE) wcopy(port, L"FILE:", MAX_PATH);
    replace_global(hmode, global_devmode(&s->dm));
    replace_global(hnames, global_devnames(s->printer, port, is_default_printer(s->printer)));
    if (hdc && (flags & (PD_RETURNDC | PD_RETURNIC))) {
        *hdc = (flags & PD_RETURNDC) ? CreateDCW(L"winspool", s->printer, NULL, &s->dm)
                                     : CreateICW(L"winspool", s->printer, NULL, &s->dm);
        if (!*hdc) { g_error = PDERR_CREATEICFAILURE; return FALSE; }
    }
    return TRUE;
}

/* The state a dialog (or PD_RETURNDEFAULT) starts from; FALSE with no printer */
static BOOL start_state(PrintDlgState *s, HGLOBAL hmode, HGLOBAL hnames)
{
    memset(s, 0, sizeof *s);
    if (!chosen_printer(hnames, hmode, s->printer)) { g_error = PDERR_NODEFAULTPRN; return FALSE; }
    DEVMODEW *in = hmode ? GlobalLock(hmode) : NULL;
    DEVMODEW copy;
    if (in) { memcpy(&copy, in, sizeof copy); GlobalUnlock(hmode); }
    if (!printer_devmode(s->printer, in ? &copy : NULL, &s->dm)) { g_error = PDERR_PRINTERNOTFOUND; return FALSE; }
    return TRUE;
}

CDAPI BOOL WINAPI PrintDlgW(LPPRINTDLGW pd)
{
    g_error = 0;
    if (!pd) { g_error = CDERR_INITIALIZATION; return FALSE; }
    if (pd->lStructSize != sizeof *pd) { g_error = CDERR_STRUCTSIZE; return FALSE; }
    if ((pd->Flags & PD_RETURNDEFAULT) && (pd->hDevMode || pd->hDevNames)) { g_error = PDERR_RETDEFFAILURE; return FALSE; }
    PrintDlgState s;
    if (!start_state(&s, pd->hDevMode, pd->hDevNames)) return FALSE;
    s.flags = pd->Flags;
    s.setup = (pd->Flags & PD_PRINTSETUP) != 0;
    s.from = pd->nFromPage; s.to = pd->nToPage; s.min = pd->nMinPage; s.max = pd->nMaxPage;
    s.copies = (pd->Flags & PD_USEDEVMODECOPIES) ? (WORD)s.dm.dmCopies : pd->nCopies;
    if (!(pd->Flags & PD_RETURNDEFAULT)) {
        if (!run(&s, pd->hwndOwner)) return FALSE;
    }
    if (!s.setup && !(pd->Flags & PD_RETURNDEFAULT)) {
        pd->Flags = s.flags;
        pd->nFromPage = s.from;
        pd->nToPage = s.to;
        if (pd->Flags & PD_USEDEVMODECOPIES) {
            s.dm.dmCopies = (short)s.copies;
            s.dm.dmCollate = (pd->Flags & PD_COLLATE) ? 1 : 0;
            pd->nCopies = 1;
        } else {
            pd->nCopies = s.copies;
        }
    }
    if (pd->Flags & PD_RETURNDEFAULT) pd->nCopies = pd->nCopies ? pd->nCopies : 1;
    pd->hDC = NULL;
    return hand_back(&s, &pd->hDevMode, &pd->hDevNames, &pd->hDC, pd->Flags);
}

/* ANSI: the DEVMODE and DEVNAMES converted both ways around the wide call */
static HGLOBAL devmode_a2w_g(HGLOBAL ga)
{
    DEVMODEA *a = ga ? GlobalLock(ga) : NULL;
    if (!a) return NULL;
    DEVMODEW w;
    memset(&w, 0, sizeof w);
    MultiByteToWideChar(CP_ACP, 0, (const char *)a->dmDeviceName, 32, w.dmDeviceName, 32);
    memcpy(&w.dmSpecVersion, &a->dmSpecVersion, offsetof(DEVMODEA, dmFormName) - offsetof(DEVMODEA, dmSpecVersion));
    MultiByteToWideChar(CP_ACP, 0, (const char *)a->dmFormName, 32, w.dmFormName, 32);
    w.dmSize = sizeof w;
    GlobalUnlock(ga);
    return global_devmode(&w);
}

static HGLOBAL devmode_w2a_g(HGLOBAL gw)
{
    DEVMODEW *w = gw ? GlobalLock(gw) : NULL;
    if (!w) return NULL;
    HGLOBAL ga = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DEVMODEA));
    DEVMODEA *a = ga ? GlobalLock(ga) : NULL;
    if (a) {
        WideCharToMultiByte(CP_ACP, 0, w->dmDeviceName, -1, (char *)a->dmDeviceName, 32, 0, 0);
        memcpy(&a->dmSpecVersion, &w->dmSpecVersion, offsetof(DEVMODEW, dmFormName) - offsetof(DEVMODEW, dmSpecVersion));
        WideCharToMultiByte(CP_ACP, 0, w->dmFormName, -1, (char *)a->dmFormName, 32, 0, 0);
        memcpy(&a->dmLogPixels, &w->dmLogPixels, sizeof *w - offsetof(DEVMODEW, dmLogPixels));
        a->dmSize = sizeof *a;
        GlobalUnlock(ga);
    }
    GlobalUnlock(gw);
    return ga;
}

static HGLOBAL devnames_conv(HGLOBAL g, BOOL to_wide)
{
    BYTE *p = g ? GlobalLock(g) : NULL;
    if (!p) return NULL;
    DEVNAMES *dn = (DEVNAMES *)p;
    WCHAR f[3][MAX_PATH];
    WORD offs[3] = { dn->wDriverOffset, dn->wDeviceOffset, dn->wOutputOffset };
    for (int i = 0; i < 3; i++) {
        if (to_wide) MultiByteToWideChar(CP_ACP, 0, (const char *)p + offs[i], -1, f[i], MAX_PATH);
        else wcopy(f[i], (const WCHAR *)p + offs[i], MAX_PATH);
        f[i][MAX_PATH - 1] = 0;
    }
    WORD dflt = dn->wDefault;
    GlobalUnlock(g);
    if (to_wide) {
        HGLOBAL w = global_devnames(f[1], f[2], dflt & DN_DEFAULTPRN);
        return w;
    }
    char a[3][MAX_PATH * 2];
    int n[3];
    for (int i = 0; i < 3; i++) n[i] = WideCharToMultiByte(CP_ACP, 0, f[i], -1, a[i], sizeof a[i], 0, 0);
    HGLOBAL r = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DEVNAMES) + n[0] + n[1] + n[2]);
    BYTE *q = r ? GlobalLock(r) : NULL;
    if (q) {
        DEVNAMES *o = (DEVNAMES *)q;
        o->wDriverOffset = sizeof(DEVNAMES);
        o->wDeviceOffset = (WORD)(o->wDriverOffset + n[0]);
        o->wOutputOffset = (WORD)(o->wDeviceOffset + n[1]);
        o->wDefault = dflt;
        memcpy(q + o->wDriverOffset, a[0], n[0]);
        memcpy(q + o->wDeviceOffset, a[1], n[1]);
        memcpy(q + o->wOutputOffset, a[2], n[2]);
        GlobalUnlock(r);
    }
    return r;
}

CDAPI BOOL WINAPI PrintDlgA(LPPRINTDLGA pd)
{
    g_error = 0;
    if (!pd) { g_error = CDERR_INITIALIZATION; return FALSE; }
    if (pd->lStructSize != sizeof *pd) { g_error = CDERR_STRUCTSIZE; return FALSE; }
    PRINTDLGW w;
    memset(&w, 0, sizeof w);
    w.lStructSize = sizeof w;
    w.hwndOwner = pd->hwndOwner;
    w.Flags = pd->Flags & ~(PD_ENABLEPRINTHOOK | PD_ENABLESETUPHOOK);
    w.nFromPage = pd->nFromPage; w.nToPage = pd->nToPage; w.nMinPage = pd->nMinPage; w.nMaxPage = pd->nMaxPage;
    w.nCopies = pd->nCopies;
    w.hDevMode = devmode_a2w_g(pd->hDevMode);
    w.hDevNames = devnames_conv(pd->hDevNames, TRUE);
    BOOL ok = PrintDlgW(&w);
    if (ok) {
        pd->Flags = w.Flags | (pd->Flags & (PD_ENABLEPRINTHOOK | PD_ENABLESETUPHOOK));
        pd->nFromPage = w.nFromPage; pd->nToPage = w.nToPage; pd->nCopies = w.nCopies;
        pd->hDC = w.hDC;
        replace_global(&pd->hDevMode, devmode_w2a_g(w.hDevMode));
        replace_global(&pd->hDevNames, devnames_conv(w.hDevNames, FALSE));
    }
    if (w.hDevMode) GlobalFree(w.hDevMode);
    if (w.hDevNames) GlobalFree(w.hDevNames);
    return ok;
}

/* PrintDlgEx: the same dialog, its answer as PRINTDLGEX gives it (the first page range) */
CDAPI HRESULT WINAPI PrintDlgExW(LPPRINTDLGEXW pd)
{
    g_error = 0;
    if (!pd) return E_INVALIDARG;
    if (pd->lStructSize != sizeof *pd) { g_error = CDERR_STRUCTSIZE; return E_INVALIDARG; }
    if (!(pd->Flags & PD_RETURNDEFAULT) && !pd->hwndOwner) return E_HANDLE;
    if ((pd->Flags & PD_PAGENUMS) && (!pd->lpPageRanges || !pd->nMaxPageRanges)) return E_INVALIDARG;
    PrintDlgState s;
    if (!start_state(&s, pd->hDevMode, pd->hDevNames)) { pd->dwResultAction = PD_RESULT_CANCEL; return E_FAIL; }
    s.flags = pd->Flags;
    s.min = (WORD)pd->nMinPage;
    s.max = (WORD)(pd->nMaxPage > 0xFFFF ? 0xFFFF : pd->nMaxPage);
    if (pd->nPageRanges && pd->lpPageRanges) { s.from = (WORD)pd->lpPageRanges[0].nFromPage; s.to = (WORD)pd->lpPageRanges[0].nToPage; }
    s.copies = (WORD)(pd->nCopies ? pd->nCopies : 1);
    if (!(pd->Flags & PD_RETURNDEFAULT) && !run(&s, pd->hwndOwner)) {
        pd->dwResultAction = PD_RESULT_CANCEL;
        return S_OK;
    }
    if (!(pd->Flags & PD_RETURNDEFAULT)) {
        pd->Flags = s.flags;
        if ((s.flags & PD_PAGENUMS) && pd->lpPageRanges && pd->nMaxPageRanges) {
            pd->lpPageRanges[0].nFromPage = s.from;
            pd->lpPageRanges[0].nToPage = s.to;
            pd->nPageRanges = 1;
        }
        if (pd->Flags & PD_USEDEVMODECOPIES) { s.dm.dmCopies = (short)s.copies; pd->nCopies = 1; }
        else pd->nCopies = s.copies;
    }
    pd->hDC = NULL;
    if (!hand_back(&s, &pd->hDevMode, &pd->hDevNames, &pd->hDC, pd->Flags)) return E_FAIL;
    pd->dwResultAction = PD_RESULT_PRINT;
    return S_OK;
}

CDAPI HRESULT WINAPI PrintDlgExA(void *pd) { (void)pd; return E_NOTIMPL; }

/* -----------------------------------------------------------------------
 * PageSetupDlg: the paper, the orientation and the margins
 * ----------------------------------------------------------------------- */
CDAPI BOOL WINAPI PageSetupDlgW(LPPAGESETUPDLGW psd)
{
    g_error = 0;
    if (!psd) { g_error = CDERR_INITIALIZATION; return FALSE; }
    if (psd->lStructSize != sizeof *psd) { g_error = CDERR_STRUCTSIZE; return FALSE; }
    if ((psd->Flags & PSD_RETURNDEFAULT) && (psd->hDevMode || psd->hDevNames)) { g_error = PDERR_RETDEFFAILURE; return FALSE; }
    PrintDlgState s;
    if (!start_state(&s, psd->hDevMode, psd->hDevNames)) return FALSE;
    s.page_setup = TRUE;
    /* the units: as asked, else the locale's (inches in the United States) */
    WCHAR measure[4] = { '1' };
    GetLocaleInfoW(0x0400 /* LOCALE_USER_DEFAULT */, LOCALE_IMEASURE, measure, 4);
    s.inches = (psd->Flags & PSD_INTHOUSANDTHSOFINCHES) ? TRUE : (psd->Flags & PSD_INHUNDREDTHSOFMILLIMETERS) ? FALSE :
               measure[0] == '1';
    if (psd->Flags & PSD_MARGINS) s.margins = psd->rtMargin;
    else { LONG m = s.inches ? 1000 : 2500; s.margins.left = s.margins.top = s.margins.right = s.margins.bottom = m; }
    if (!(psd->Flags & PSD_RETURNDEFAULT) && !run(&s, psd->hwndOwner)) return FALSE;
    /* the paper's size, in the units */
    int tw = 2159, th = 2794;
    WORD papers[64];
    POINT sizes[64];
    int n = DeviceCapabilitiesW(s.printer, NULL, DC_PAPERS, NULL, NULL);
    if (n > 0 && n <= 64 && DeviceCapabilitiesW(s.printer, NULL, DC_PAPERS, (LPWSTR)papers, NULL) == n &&
        DeviceCapabilitiesW(s.printer, NULL, DC_PAPERSIZE, (LPWSTR)sizes, NULL) == n)
        for (int i = 0; i < n; i++) if (papers[i] == (WORD)s.dm.dmPaperSize) { tw = sizes[i].x; th = sizes[i].y; }
    if (s.dm.dmOrientation == DMORIENT_LANDSCAPE) { int t = tw; tw = th; th = t; }
    psd->ptPaperSize.x = s.inches ? (LONG)((long long)tw * 1000 / 254) : tw * 10;
    psd->ptPaperSize.y = s.inches ? (LONG)((long long)th * 1000 / 254) : th * 10;
    memset(&psd->rtMinMargin, 0, sizeof psd->rtMinMargin);
    psd->rtMargin = s.margins;
    psd->Flags = (psd->Flags & ~(PSD_INTHOUSANDTHSOFINCHES | PSD_INHUNDREDTHSOFMILLIMETERS)) |
                 (s.inches ? PSD_INTHOUSANDTHSOFINCHES : PSD_INHUNDREDTHSOFMILLIMETERS);
    return hand_back(&s, &psd->hDevMode, &psd->hDevNames, NULL, 0);
}

CDAPI BOOL WINAPI PageSetupDlgA(void *psd) { (void)psd; g_error = CDERR_INITIALIZATION; return FALSE; }
