/* textservtest.exe — the windowless Rich Edit (userland/textserv/textserv.c)
 * as XAML's text boxes use it: this program is the host (ITextHost2, the
 * way XAML's TextServicesHost is), loads WinUIEdit.dll by name, checks
 * the IIDs it exports, creates the text services with CreateTextServices
 * and drives them with TxSendMessage: typing, Backspace and the editing
 * keys, the selection, the clipboard, undo and redo, the text limit,
 * read-only, single and multiple lines with word wrap, a password box,
 * the mouse, and the notifications the host gets (EN_CHANGE,
 * EN_SELCHANGE, EN_MAXTEXT, the caret).  TxDraw draws into a bitmap whose
 * pixels are checked: text, the selection's highlight, and a password
 * box's text drawn as the password characters.  The Text Object Model as
 * XAML's TextBoxBase uses it (ITextDocument2 by QueryInterface, ranges,
 * the selection, units, points, undo with tomSuspend, Freeze, the
 * clipboard; IRichEditOle by EM_GETOLEINTERFACE), ranges following edits
 * and outliving the text services, and TxDrawD2D onto a Direct2D render
 * target (opaque, and transparent over what is there).  The cut, copy and
 * paste keys offered to the host first (EN_*REQUESTED), and the Windows
 * Runtime document a XAML RichEditBox hands out (Microsoft.UI.Text's
 * ITextDocument, ranges and selection).  Built for 32 bits
 * too, where ITextServices' methods are thiscall, as on Windows.
 *
 *   textservtest    the test */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <novatextserv.h>
#include <winstring.h>

static int pass, fail;
static void check(const char *what, int ok)
{
    if (ok) pass++;
    else printf("FAIL: %s\n", what), fail++;
}

/* ---- the host ----------------------------------------------------------- */
typedef struct {
    ITextHost2 iface;
    LONG ref;
    DWORD bits;
    RECT client;
    CHARFORMATW cf;
    PARAFORMAT pf;
    int changes, textchanges, selchanges, maxtext, carets, caretshown, focusreq, capture;
    CHARRANGE lastsel;
    POINT caret;
    HDC dc;                                     /* TxGetDC's */
    TXTBACKSTYLE back;
    int requests[3];                            /* EN_PASTEREQUESTED, EN_COPYREQUESTED, EN_CUTREQUESTED */
    int handle_requests;                        /* (answer them handled: the host does it its own way) */
} HOSTOBJ;

#define H(p) ((HOSTOBJ *)(p))
static const IID iid_unknown = {0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID iid_host = NOVA_IID_ITextHost, iid_host2 = NOVA_IID_ITextHost2;
static const IID iid_ts = NOVA_IID_ITextServices, iid_ts2 = NOVA_IID_ITextServices2;

static HRESULT STDMETHODCALLTYPE h_qi(ITextHost2 *me, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &iid_unknown) || IsEqualGUID(iid, &iid_host) || IsEqualGUID(iid, &iid_host2)) {
        *out = me;
        H(me)->ref++;
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE h_addref(ITextHost2 *me) { return ++H(me)->ref; }
static ULONG STDMETHODCALLTYPE h_release(ITextHost2 *me) { return --H(me)->ref; }
static HDC TXAPI h_getdc(ITextHost2 *me) { return H(me)->dc; }
static INT TXAPI h_releasedc(ITextHost2 *me, HDC dc) { (void)me; (void)dc; return 1; }
static BOOL TXAPI h_showsb(ITextHost2 *me, INT b, BOOL s) { (void)me; (void)b; (void)s; return TRUE; }
static BOOL TXAPI h_enablesb(ITextHost2 *me, INT f, INT a) { (void)me; (void)f; (void)a; return TRUE; }
static BOOL TXAPI h_sbrange(ITextHost2 *me, INT b, LONG mn, INT mx, BOOL r) { (void)me; (void)b; (void)mn; (void)mx; (void)r; return TRUE; }
static BOOL TXAPI h_sbpos(ITextHost2 *me, INT b, INT p, BOOL r) { (void)me; (void)b; (void)p; (void)r; return TRUE; }
static void TXAPI h_invalidate(ITextHost2 *me, LPCRECT rc, BOOL m) { (void)me; (void)rc; (void)m; }
static void TXAPI h_viewchange(ITextHost2 *me, BOOL u) { (void)me; (void)u; }
static BOOL TXAPI h_createcaret(ITextHost2 *me, HBITMAP b, INT w, INT h) { (void)b; (void)w; (void)h; H(me)->carets++; return TRUE; }
static BOOL TXAPI h_showcaret(ITextHost2 *me, BOOL s) { H(me)->caretshown = s; return TRUE; }
static BOOL TXAPI h_caretpos(ITextHost2 *me, INT x, INT y) { H(me)->caret.x = x; H(me)->caret.y = y; return TRUE; }
static BOOL TXAPI h_settimer(ITextHost2 *me, UINT id, UINT t) { (void)me; (void)id; (void)t; return TRUE; }
static void TXAPI h_killtimer(ITextHost2 *me, UINT id) { (void)me; (void)id; }
static void TXAPI h_scroll(ITextHost2 *me, INT dx, INT dy, LPCRECT a, LPCRECT b, HRGN r, LPRECT u, UINT f)
{ (void)me; (void)dx; (void)dy; (void)a; (void)b; (void)r; (void)u; (void)f; }
static void TXAPI h_capture(ITextHost2 *me, BOOL c) { H(me)->capture = c; }
static void TXAPI h_setfocus(ITextHost2 *me) { H(me)->focusreq++; }
static void TXAPI h_setcursor(ITextHost2 *me, HCURSOR c, BOOL t) { (void)me; (void)c; (void)t; }
static BOOL TXAPI h_s2c(ITextHost2 *me, LPPOINT p) { (void)me; (void)p; return TRUE; }
static BOOL TXAPI h_c2s(ITextHost2 *me, LPPOINT p) { (void)me; (void)p; return TRUE; }
static HRESULT TXAPI h_activate(ITextHost2 *me, LONG *o) { (void)me; if (o) *o = 0; return S_OK; }
static HRESULT TXAPI h_deactivate(ITextHost2 *me, LONG s) { (void)me; (void)s; return S_OK; }
static HRESULT TXAPI h_clientrect(ITextHost2 *me, LPRECT rc) { *rc = H(me)->client; return S_OK; }
static HRESULT TXAPI h_inset(ITextHost2 *me, LPRECT rc) { (void)me; SetRect(rc, 0, 0, 0, 0); return S_OK; }
static HRESULT TXAPI h_charformat(ITextHost2 *me, const CHARFORMATW **cf) { *cf = &H(me)->cf; return S_OK; }
static HRESULT TXAPI h_paraformat(ITextHost2 *me, const PARAFORMAT **pf) { *pf = &H(me)->pf; return S_OK; }
static COLORREF TXAPI h_syscolor(ITextHost2 *me, int i)
{
    (void)me;
    switch (i) {                                /* (fixed colours, to find in the bitmap) */
    case COLOR_WINDOW: return RGB(255, 255, 255);
    case COLOR_WINDOWTEXT: return RGB(0, 0, 0);
    case COLOR_HIGHLIGHT: return RGB(0, 120, 215);
    case COLOR_HIGHLIGHTTEXT: return RGB(255, 255, 255);
    }
    return GetSysColor(i);
}
static HRESULT TXAPI h_backstyle(ITextHost2 *me, TXTBACKSTYLE *s) { *s = H(me)->back; return S_OK; }
static HRESULT TXAPI h_maxlen(ITextHost2 *me, DWORD *n) { (void)me; *n = INFINITE;   /* (as XAML's text boxes: no limit) */ return S_OK; }
static HRESULT TXAPI h_scrollbars(ITextHost2 *me, DWORD *b) { (void)me; *b = 0; return S_OK; }
static HRESULT TXAPI h_pwchar(ITextHost2 *me, WCHAR *c) { (void)me; *c = 0x25cf; return S_OK; }   /* ● */
static HRESULT TXAPI h_accel(ITextHost2 *me, LONG *cp) { (void)me; *cp = -1; return S_OK; }
static HRESULT TXAPI h_extent(ITextHost2 *me, LPSIZEL e) { (void)me; (void)e; return E_NOTIMPL; }
static HRESULT TXAPI h_cfchange(ITextHost2 *me, const CHARFORMATW *cf) { (void)me; (void)cf; return S_OK; }
static HRESULT TXAPI h_pfchange(ITextHost2 *me, const PARAFORMAT *pf) { (void)me; (void)pf; return S_OK; }
static HRESULT TXAPI h_bits(ITextHost2 *me, DWORD mask, DWORD *bits) { *bits = H(me)->bits & mask; return S_OK; }
static HRESULT TXAPI h_notify(ITextHost2 *me, DWORD code, void *data)
{
    if (code == EN_CHANGE) { H(me)->changes++; if (data && (((CHANGENOTIFY *)data)->dwChangeType & CN_TEXTCHANGED)) H(me)->textchanges++; }
    else if (code == EN_SELCHANGE) { H(me)->selchanges++; H(me)->lastsel = ((SELCHANGE *)data)->chrg; }
    else if (code == EN_MAXTEXT) H(me)->maxtext++;
    else if (code >= 0x0730 && code <= 0x0732) {  /* (WinUIEdit's cut/copy/paste requests: data is a bool) */
        H(me)->requests[code - 0x0730]++;
        if (H(me)->handle_requests) *(unsigned char *)data = 1;
    }
    return S_OK;
}
static HIMC_ TXAPI h_imm(ITextHost2 *me) { (void)me; return 0; }
static void TXAPI h_immrelease(ITextHost2 *me, HIMC_ h) { (void)me; (void)h; }
static HRESULT TXAPI h_selbar(ITextHost2 *me, LONG *w) { (void)me; *w = 0; return S_OK; }
static BOOL TXAPI h_dblclick(ITextHost2 *me) { (void)me; return FALSE; }
static HRESULT TXAPI h_window(ITextHost2 *me, HWND *w) { (void)me; *w = 0; return S_OK; }
static HRESULT TXAPI h_foreground(ITextHost2 *me) { (void)me; return S_OK; }
static HPALETTE TXAPI h_palette(ITextHost2 *me) { (void)me; return 0; }
static HRESULT TXAPI h_eaflags(ITextHost2 *me, LONG *f) { (void)me; *f = 0; return S_OK; }
static HCURSOR TXAPI h_setcursor2(ITextHost2 *me, HCURSOR c, BOOL t) { (void)me; (void)t; return c; }
static void TXAPI h_freenotif(ITextHost2 *me) { (void)me; }
static HRESULT TXAPI h_editstyle(ITextHost2 *me, DWORD i, DWORD *d) { (void)me; (void)i; *d = 0; return S_OK; }
static HRESULT TXAPI h_winstyles(ITextHost2 *me, DWORD *s, DWORD *x) { (void)me; *s = 0; *x = 0; return S_OK; }
static HRESULT TXAPI h_dropcaret(ITextHost2 *me, BOOL s, HDC dc, LPCRECT rc) { (void)me; (void)s; (void)dc; (void)rc; return S_OK; }
static HRESULT TXAPI h_destroycaret(ITextHost2 *me) { (void)me; return S_OK; }
static HRESULT TXAPI h_horzextent(ITextHost2 *me, LONG *e) { (void)me; *e = 0; return S_OK; }

static const ITextHost2Vtbl host_vtbl = {
    h_qi, h_addref, h_release, h_getdc, h_releasedc, h_showsb, h_enablesb, h_sbrange, h_sbpos,
    h_invalidate, h_viewchange, h_createcaret, h_showcaret, h_caretpos, h_settimer, h_killtimer,
    h_scroll, h_capture, h_setfocus, h_setcursor, h_s2c, h_c2s, h_activate, h_deactivate,
    h_clientrect, h_inset, h_charformat, h_paraformat, h_syscolor, h_backstyle, h_maxlen,
    h_scrollbars, h_pwchar, h_accel, h_extent, h_cfchange, h_pfchange, h_bits, h_notify,
    h_imm, h_immrelease, h_selbar, h_dblclick, h_window, h_foreground, h_palette, h_eaflags,
    h_setcursor2, h_freenotif, h_editstyle, h_winstyles, h_dropcaret, h_destroycaret, h_horzextent,
};

static void host_init(HOSTOBJ *h, DWORD bits, int width, HDC dc)
{
    memset(h, 0, sizeof(*h));
    h->iface.lpVtbl = &host_vtbl;
    h->ref = 1;
    h->bits = bits;
    SetRect(&h->client, 0, 0, width, 24);
    h->cf.cbSize = sizeof(h->cf);
    h->cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
    h->cf.dwEffects = CFE_AUTOCOLOR;
    h->cf.yHeight = 180;
    wcscpy(h->cf.szFaceName, L"Segoe UI");
    h->pf.cbSize = sizeof(h->pf);
    h->dc = dc;
    h->back = TXTBACK_OPAQUE;
}

/* ---- helpers ------------------------------------------------------------ */
static ITextServices2 *ts;
static HOSTOBJ host;

static LRESULT msg(UINT m, WPARAM wp, LPARAM lp)
{
    LRESULT r = 0;
    ts->lpVtbl->TxSendMessage(ts, m, wp, lp, &r);
    return r;
}
static HRESULT msg_hr(UINT m, WPARAM wp, LPARAM lp)
{
    LRESULT r = 0;
    return ts->lpVtbl->TxSendMessage(ts, m, wp, lp, &r);
}
static void type(const WCHAR *s) { while (*s) msg(WM_CHAR, *s++, 1); }
static void key(UINT vk) { msg(WM_KEYDOWN, vk, 1); }

static int text_is(const WCHAR *want)
{
    BSTR b = 0;
    int ok;
    if (ts->lpVtbl->TxGetText(ts, &b) != S_OK) return 0;
    ok = wcscmp(b ? b : L"", want) == 0;
    if (!ok) printf("  text is \"%ls\", want \"%ls\"\n", b ? b : L"", want);
    SysFreeString(b);
    return ok;
}

static void set_sel(LONG a, LONG b) { CHARRANGE cr = {a, b}; msg(EM_EXSETSEL, 0, (LPARAM)&cr); }
static CHARRANGE get_sel(void) { CHARRANGE cr = {-1, -1}; msg(EM_EXGETSEL, 0, (LPARAM)&cr); return cr; }

static int clipboard_is(const WCHAR *want)
{
    HANDLE h;
    int ok = 0;
    if (!OpenClipboard(0)) return 0;
    if ((h = GetClipboardData(CF_UNICODETEXT))) {
        const WCHAR *p = GlobalLock(h);
        ok = p && wcscmp(p, want) == 0;
        if (!ok) printf("  clipboard is \"%ls\", want \"%ls\"\n", p ? p : L"(null)", want);
        GlobalUnlock(h);
    }
    CloseClipboard();
    return ok;
}

static ITextServices2 *create(PCREATETEXTSERVICES fn, HOSTOBJ *h, IUnknown **unk)
{
    ITextServices2 *s = 0;
    *unk = 0;
    if (fn(0, (ITextHost *)&h->iface, unk) != S_OK || !*unk) return 0;
    (*unk)->lpVtbl->QueryInterface(*unk, &iid_ts2, (void **)&s);
    return s;
}

/* a 32-bit DIB section to draw into, and what is in it */
#define BW 240
#define BH 24
static DWORD *bits_of;
static HBITMAP make_bitmap(HDC dc)
{
    BITMAPINFO bi;
    void *p = 0;
    HBITMAP bmp;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = BW;
    bi.bmiHeader.biHeight = -BH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &p, 0, 0);
    bits_of = p;
    return bmp;
}
static int count_pixels(DWORD rgb)               /* (DIB pixels are 0x00RRGGBB) */
{
    int i, n = 0;
    for (i = 0; i < BW * BH; i++) n += (bits_of[i] & 0xffffff) == rgb;
    return n;
}
static int count_dark(void)
{
    int i, n = 0;
    for (i = 0; i < BW * BH; i++) {
        DWORD p = bits_of[i];
        n += ((p >> 16) & 0xff) < 128 && ((p >> 8) & 0xff) < 128 && (p & 0xff) < 128;
    }
    return n;
}
static void draw(HDC dc)
{
    RECTL rc = {0, 0, BW, BH};
    ts->lpVtbl->TxDraw(ts, DVASPECT_CONTENT, 0, 0, 0, dc, 0, &rc, 0, 0, 0, 0, TXTVIEW_ACTIVE);
    GdiFlush();
}

/* ---- the tests ---------------------------------------------------------- */
static void exports(HMODULE m, const char *name)
{
    static const struct { const char *sym; IID iid; } iids[] = {
        {"IID_ITextServices", NOVA_IID_ITextServices}, {"IID_ITextServices2", NOVA_IID_ITextServices2},
        {"IID_ITextHost", NOVA_IID_ITextHost}, {"IID_ITextHost2", NOVA_IID_ITextHost2},
        {"IID_ITextDocument2", NOVA_IID_ITextDocument2},
        {"IID_IRicheditWindowlessAccessibility", NOVA_IID_IRicheditWindowlessAccessibility},
    };
    char what[160];
    unsigned i;
    for (i = 0; i < sizeof(iids) / sizeof(iids[0]); i++) {
        const IID *p = (const IID *)GetProcAddress(m, iids[i].sym);
        snprintf(what, sizeof(what), "%s exports %s", name, iids[i].sym);
        check(what, p && IsEqualGUID(p, &iids[i].iid));
    }
    snprintf(what, sizeof(what), "%s exports CreateTextServices and ShutdownTextServices", name);
    check(what, GetProcAddress(m, "CreateTextServices") && GetProcAddress(m, "ShutdownTextServices"));
}

static void typing(void)
{
    LONG x0;
    msg(EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
    check("EM_GETEVENTMASK", msg(EM_GETEVENTMASK, 0, 0) == (ENM_CHANGE | ENM_SELCHANGE));
    msg(WM_SETFOCUS, 0, 0);
    check("focus: a caret, shown", host.carets == 1 && host.caretshown);
    x0 = host.caret.x;
    type(L"Hello");
    check("typing: the text", text_is(L"Hello"));
    check("typing: EN_CHANGE for each character", host.changes == 5);
    check("EN_CHANGE says the text changed (CHANGENOTIFY, CN_TEXTCHANGED)", host.textchanges == 5);
    check("typing: the caret moved right", host.caret.x > x0);
    check("typing: the selection is the caret, after the text", get_sel().cpMin == 5 && get_sel().cpMax == 5);
    msg(WM_CHAR, 8, 1);
    check("Backspace", text_is(L"Hell"));
    type(L"o world");
    key(VK_HOME);
    check("Home", get_sel().cpMin == 0 && host.lastsel.cpMax == 0);
    key(VK_RIGHT); key(VK_RIGHT); key(VK_RIGHT); key(VK_RIGHT); key(VK_RIGHT);
    type(L",");
    check("Right x5 and typing there", text_is(L"Hello, world"));
    key(VK_END);
    check("End", get_sel().cpMin == 12);
    key(VK_LEFT);
    key(VK_DELETE);
    check("Left, Delete", text_is(L"Hello, worl"));
    type(L"d");
    check("the text through WM_GETTEXTLENGTH", msg(WM_GETTEXTLENGTH, 0, 0) == 12);
    check("Enter on one line goes to the host", msg_hr(WM_CHAR, L'\r', 1) == S_FALSE && text_is(L"Hello, world"));
}

static void selection_and_clipboard(void)
{
    WCHAR buf[64];
    int sels = host.selchanges;
    set_sel(7, 12);
    check("EM_EXSETSEL: EN_SELCHANGE with the range", host.selchanges == sels + 1 && host.lastsel.cpMin == 7 && host.lastsel.cpMax == 12);
    check("EM_GETSELTEXT", msg(EM_GETSELTEXT, 0, (LPARAM)buf) == 5 && wcscmp(buf, L"world") == 0);
    check("EM_SELECTIONTYPE", msg(EM_SELECTIONTYPE, 0, 0) == SEL_TEXT);
    msg(WM_COPY, 0, 0);
    check("WM_COPY", clipboard_is(L"world"));
    check("EM_CANPASTE", msg(EM_CANPASTE, 0, 0) != 0);
    key(VK_END);
    type(L" ");
    msg(WM_PASTE, 0, 0);
    check("WM_PASTE", text_is(L"Hello, world world"));
    set_sel(0, 7);
    msg(WM_CUT, 0, 0);
    check("WM_CUT: the text", text_is(L"world world"));
    check("WM_CUT: the clipboard", clipboard_is(L"Hello, "));
    check("EM_CANUNDO", msg(EM_CANUNDO, 0, 0) != 0);
    msg(EM_UNDO, 0, 0);
    check("EM_UNDO puts the cut text back", text_is(L"Hello, world world"));
    check("EM_CANREDO", msg(EM_CANREDO, 0, 0) != 0);
    msg(EM_REDO, 0, 0);
    check("EM_REDO cuts it again", text_is(L"world world"));
    set_sel(0, 5);
    msg(EM_REPLACESEL, TRUE, (LPARAM)L"big");
    check("EM_REPLACESEL", text_is(L"big world"));
    set_sel(0, -1);
    check("EM_EXSETSEL to -1 selects all", get_sel().cpMin == 0 && get_sel().cpMax == 9);
    msg(WM_CLEAR, 0, 0);
    check("WM_CLEAR", text_is(L""));
    /* typing is one undo step */
    type(L"abc");
    msg(EM_UNDO, 0, 0);
    check("typing undoes as one step", text_is(L""));
    msg(WM_SETTEXT, 0, (LPARAM)L"one\r\ntwo");
    check("one line keeps the first paragraph", text_is(L"one"));
    check("WM_SETTEXT empties the undo", msg(EM_CANUNDO, 0, 0) == 0);
    {
        SETTEXTEX st = {ST_SELECTION, 1200};
        GETTEXTEX gt = {sizeof(buf), GT_DEFAULT, 1200, 0, 0};
        set_sel(3, 3);
        msg(EM_SETTEXTEX, (WPARAM)&st, (LPARAM)L" two");
        check("EM_SETTEXTEX ST_SELECTION, EM_GETTEXTEX", msg(EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)buf) == 7 && wcscmp(buf, L"one two") == 0);
    }
}

static void limits(void)
{
    msg(WM_SETTEXT, 0, (LPARAM)L"");
    msg(EM_EXLIMITTEXT, 0, 5);
    host.maxtext = 0;
    type(L"123456");
    check("EM_EXLIMITTEXT: no more than the limit", text_is(L"12345"));
    check("EM_EXLIMITTEXT: EN_MAXTEXT", host.maxtext == 1);
    msg(EM_EXLIMITTEXT, 0, 1000);
    msg(EM_SETREADONLY, TRUE, 0);
    type(L"x");
    msg(WM_CHAR, 8, 1);
    msg(WM_PASTE, 0, 0);
    check("EM_SETREADONLY: no typing, Backspace or paste", text_is(L"12345"));
    msg(EM_SETREADONLY, FALSE, 0);
    msg(EM_SETMODIFY, FALSE, 0);
    type(L"6");
    check("EM_GETMODIFY", msg(EM_GETMODIFY, 0, 0) != 0 && text_is(L"123456"));
}

static void mouse(void)
{
    POINTL pt;
    LONG cp;
    msg(WM_SETTEXT, 0, (LPARAM)L"click here");
    msg(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(200, 5));
    check("click past the end: the caret at the end", get_sel().cpMin == 10 && get_sel().cpMax == 10 && host.capture);
    msg(WM_LBUTTONUP, 0, MAKELPARAM(200, 5));
    msg(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(0, 5));
    msg(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(200, 5));
    msg(WM_LBUTTONUP, 0, MAKELPARAM(200, 5));
    check("drag: selects", get_sel().cpMin == 0 && get_sel().cpMax == 10 && !host.capture);
    msg(EM_POSFROMCHAR, (WPARAM)&pt, 6);
    pt.x += 1;
    cp = (LONG)msg(EM_CHARFROMPOS, 0, (LPARAM)&pt);
    check("EM_POSFROMCHAR and EM_CHARFROMPOS agree", cp == 6 && pt.x > 10);
    msg(WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(pt.x + 2, 5));
    msg(WM_LBUTTONUP, 0, 0);
    check("double-click selects the word", get_sel().cpMin == 6 && get_sel().cpMax == 10);
}

static void multiline(void)
{
    WCHAR buf[32];
    LONG w = 0, h = 0, w2, h2 = 0, lineh;
    host.bits |= TXTBIT_MULTILINE | TXTBIT_WORDWRAP;
    ts->lpVtbl->OnTxPropertyBitsChange(ts, TXTBIT_MULTILINE | TXTBIT_WORDWRAP, host.bits);
    msg(WM_SETTEXT, 0, (LPARAM)L"one\r\ntwo");
    check("multi-line: paragraphs end in \\r", text_is(L"one\rtwo"));
    check("multi-line: WM_GETTEXT gives \\r\\n", msg(WM_GETTEXT, 32, (LPARAM)buf) == 8 && wcscmp(buf, L"one\r\ntwo") == 0);
    check("multi-line: EM_GETLINECOUNT", msg(EM_GETLINECOUNT, 0, 0) == 2);
    key(VK_DOWN);
    check("multi-line: Down", msg(EM_LINEFROMCHAR, get_sel().cpMin, 0) == 1);
    key(VK_END);
    msg(WM_CHAR, L'\r', 1);
    type(L"three");
    check("multi-line: Enter starts a paragraph", text_is(L"one\rtwo\rthree") && msg(EM_GETLINECOUNT, 0, 0) == 3);
    ts->lpVtbl->TxGetNaturalSize(ts, DVASPECT_CONTENT, 0, 0, 0, TXTNS_FITTOCONTENT, 0, &w, &h);
    check("TxGetNaturalSize: three lines high", w > 10 && h > 20 && h % 3 == 0);
    lineh = h / 3;
    msg(WM_SETTEXT, 0, (LPARAM)L"aaa bbb ccc ddd eee fff");
    host.client.right = 50;
    check("word wrap in a narrow box", msg(EM_GETLINECOUNT, 0, 0) >= 3);
    w2 = 50;
    ts->lpVtbl->TxGetNaturalSize(ts, DVASPECT_CONTENT, 0, 0, 0, TXTNS_FITTOCONTENT, 0, &w2, &h2);
    check("TxGetNaturalSize wraps to the width", w2 <= 51 && h2 >= 3 * lineh);
    host.client.right = BW;
    host.bits &= ~(TXTBIT_MULTILINE | TXTBIT_WORDWRAP);
    ts->lpVtbl->OnTxPropertyBitsChange(ts, TXTBIT_MULTILINE | TXTBIT_WORDWRAP, host.bits);
    check("back to one line: the first paragraph", text_is(L"aaa bbb ccc ddd eee fff"));
}

static void drawing(HDC dc)
{
    int dark, hl;
    msg(WM_SETTEXT, 0, (LPARAM)L"Drawn text");
    set_sel(0, 0);
    draw(dc);
    dark = count_dark();
    check("TxDraw: the text is drawn", dark > 20);
    check("TxDraw: no highlight without a selection", count_pixels(0x0078d7) == 0);
    set_sel(0, 5);
    draw(dc);
    hl = count_pixels(0x0078d7);
    check("TxDraw: the selection is highlighted", hl > 50);
    check("TxDraw: the selected text is drawn on it", count_dark() < dark);
    msg(WM_KILLFOCUS, 0, 0);
    check("focus lost: the caret hidden", !host.caretshown);
    host.bits |= TXTBIT_HIDESELECTION;
    ts->lpVtbl->OnTxPropertyBitsChange(ts, TXTBIT_HIDESELECTION, host.bits);
    draw(dc);
    check("TXTBIT_HIDESELECTION: no highlight without focus", count_pixels(0x0078d7) == 0);
    host.bits &= ~TXTBIT_HIDESELECTION;
    ts->lpVtbl->OnTxPropertyBitsChange(ts, TXTBIT_HIDESELECTION, host.bits);
    msg(WM_SETFOCUS, 0, 0);
}

/* a password box draws its password character for each one, and never copies */
static void password(HDC dc)
{
    DWORD *a = malloc(BW * BH * 4);
    HRESULT hr;
    msg(WM_SETTEXT, 0, (LPARAM)L"\x25cf\x25cf\x25cf\x25cf\x25cf\x25cf");
    set_sel(0, 0);
    draw(dc);
    memcpy(a, bits_of, BW * BH * 4);
    host.bits |= TXTBIT_USEPASSWORD;
    hr = ts->lpVtbl->OnTxPropertyBitsChange(ts, TXTBIT_USEPASSWORD, host.bits);
    msg(WM_SETTEXT, 0, (LPARAM)L"secret");
    set_sel(0, 0);
    draw(dc);
    check("password: drawn as the password characters", hr == S_OK && memcmp(a, bits_of, BW * BH * 4) == 0);
    check("password: the text is kept", text_is(L"secret"));
    OpenClipboard(0); EmptyClipboard(); CloseClipboard();
    {
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 16);
        wcscpy(GlobalLock(g), L"before");
        GlobalUnlock(g);
        OpenClipboard(0); SetClipboardData(CF_UNICODETEXT, g); CloseClipboard();
    }
    set_sel(0, -1);
    msg(WM_COPY, 0, 0);
    msg(WM_CUT, 0, 0);
    check("password: no copy or cut", clipboard_is(L"before") && text_is(L"secret"));
    host.bits &= ~TXTBIT_USEPASSWORD;
    ts->lpVtbl->OnTxPropertyBitsChange(ts, TXTBIT_USEPASSWORD, host.bits);
    free(a);
}

/* the other DLLs give the same text services */
static void others(void)
{
    static const char *names[] = {"msftedit.dll", "riched20.dll"};
    unsigned i;
    for (i = 0; i < 2; i++) {
        HMODULE m = LoadLibraryA(names[i]);
        PCREATETEXTSERVICES fn = m ? (PCREATETEXTSERVICES)GetProcAddress(m, "CreateTextServices") : 0;
        HOSTOBJ h;
        IUnknown *unk = 0;
        ITextServices2 *s;
        char what[96];
        LRESULT r = 0;
        BSTR b = 0;
        host_init(&h, 0, BW, 0);
        s = fn ? create(fn, &h, &unk) : 0;
        if (s) {
            s->lpVtbl->TxSendMessage(s, WM_CHAR, L'x', 1, &r);
            s->lpVtbl->TxGetText(s, &b);
        }
        snprintf(what, sizeof(what), "%s: CreateTextServices, typing", names[i]);
        check(what, s && b && wcscmp(b, L"x") == 0);
        SysFreeString(b);
        if (s) s->lpVtbl->Release(s);
        if (unk) unk->lpVtbl->Release(unk);
    }
}

/* ---- the Text Object Model ---------------------------------------------- */
static const IID iid_doc2 = NOVA_IID_ITextDocument2, iid_doc = NOVA_IID_ITextDocument;
static const IID iid_range2 = NOVA_IID_ITextRange2, iid_sel2 = NOVA_IID_ITextSelection2;

static int bstr_is(BSTR b, const WCHAR *want) { return b && wcscmp(b, want) == 0; }

static int range_text_is(ITextRange2 *r, const WCHAR *want)
{
    BSTR b = 0;
    int ok = r->lpVtbl->GetText(r, &b) == S_OK && bstr_is(b, want);
    if (!ok) printf("  (range text: \"%ls\", want \"%ls\")\n", b ? b : L"(null)", want);
    SysFreeString(b);
    return ok;
}

static int ends_are(ITextRange2 *r, LONG s, LONG e)
{
    LONG a = -1, b = -1;
    r->lpVtbl->GetStart(r, &a);
    r->lpVtbl->GetEnd(r, &b);
    if (a != s || b != e) printf("  (range %ld..%ld, want %ld..%ld)\n", (long)a, (long)b, (long)s, (long)e);
    return a == s && b == e;
}

static ITextRange2 *range(ITextDocument2 *d, LONG s, LONG e)
{
    ITextRange2 *r = 0;
    d->lpVtbl->Range2(d, s, e, &r);              /* (active, anchor) */
    return r;
}

static ITextRange2 *kept;                       /* (a range that outlives the text services) */

static void tom(HMODULE m)
{
    const IID *exported = (const IID *)GetProcAddress(m, "IID_ITextDocument2");
    ITextDocument2 *d = 0, *d1 = 0, *d2 = 0;
    IRichEditOle_ *ole = 0;
    ITextSelection2 *sel = 0, *sel2 = 0;
    ITextRange2 *r, *w;
    LONG n = 0, x = 0, y = 0, x2 = 0, y2 = 0, delta = 0;
    BSTR b = 0;
    LRESULT lr = 0;

    check("TOM: QueryInterface(IID_ITextDocument2) as XAML does",
          exported && ts->lpVtbl->QueryInterface(ts, exported, (void **)&d) == S_OK && d);
    if (!d) return;
    ts->lpVtbl->QueryInterface(ts, &iid_doc, (void **)&d1);
    ts->lpVtbl->TxSendMessage(ts, EM_GETOLEINTERFACE, 0, (LPARAM)&ole, &lr);
    if (ole) ole->lpVtbl->QueryInterface(ole, &iid_doc2, (void **)&d2);
    check("TOM: ITextDocument and EM_GETOLEINTERFACE's IRichEditOle give the same document",
          d1 == d && lr && ole && d2 == d && ole->lpVtbl->GetObjectCount(ole) == 0);
    if (d1) d1->lpVtbl->Release(d1);
    if (d2) d2->lpVtbl->Release(d2);
    if (ole) ole->lpVtbl->Release(ole);

    msg(WM_SETTEXT, 0, (LPARAM)L"Hello world");
    r = range(d, 0, tomForward);
    check("Range2(0, tomForward): the story and its final paragraph mark", r && ends_are(r, 0, 12));
    r->lpVtbl->GetStoryLength(r, &n);
    check("GetStoryLength", n == 12);
    check("GetText leaves out the final paragraph mark", range_text_is(r, L"Hello world"));
    r->lpVtbl->GetText2(r, tomAllowFinalEOP, &b);
    check("GetText2(tomAllowFinalEOP): plain text has no final mark (XAML's TextBox.Text)", bstr_is(b, L"Hello world"));
    SysFreeString(b);
    b = 0;
    host.bits |= TXTBIT_RICHTEXT;
    ts->lpVtbl->OnTxPropertyBitsChange(ts, TXTBIT_RICHTEXT, host.bits);
    r->lpVtbl->GetText2(r, tomAllowFinalEOP, &b);
    check("GetText2(tomAllowFinalEOP): rich text ends in its final mark", bstr_is(b, L"Hello world\r"));
    SysFreeString(b);
    b = 0;
    host.bits &= ~TXTBIT_RICHTEXT;
    ts->lpVtbl->OnTxPropertyBitsChange(ts, TXTBIT_RICHTEXT, host.bits);
    r->lpVtbl->Release(r);

    w = range(d, 7, 7);
    check("Expand(tomWord)", w->lpVtbl->Expand(w, tomWord, &delta) == S_OK && ends_are(w, 6, 11) && delta == 5);
    w->lpVtbl->GetChar(w, &n);
    check("GetChar", n == L'w');
    r = range(d, 0, 5);
    b = SysAllocString(L"Howdy");
    host.changes = 0;
    check("SetText replaces the range's text", r->lpVtbl->SetText(r, b) == S_OK && text_is(L"Howdy world")
          && ends_are(r, 0, 5) && host.changes == 1);
    SysFreeString(b);
    set_sel(0, 0);
    type(L"Oh ");
    check("ranges follow edits made elsewhere", range_text_is(w, L"world") && ends_are(w, 9, 14) && ends_are(r, 3, 8));
    r->lpVtbl->Release(r);

    /* the selection, as XAML's TextBoxBase drives it */
    d->lpVtbl->GetSelection2(d, &sel);
    d->lpVtbl->GetSelection(d, &sel2);
    check("GetSelection2: one selection object", sel && sel == sel2);
    if (sel2) sel2->lpVtbl->Release(sel2);
    if (!sel) { w->lpVtbl->Release(w); d->lpVtbl->Release(d); return; }
    {
        void *p1 = 0, *p2 = 0, *p3 = 0;
        check("QueryInterface: ITextRange2 on both, ITextSelection2 on the selection only",
              w->lpVtbl->QueryInterface(w, &iid_range2, &p1) == S_OK && p1 == w
              && w->lpVtbl->QueryInterface(w, &iid_sel2, &p2) == E_NOINTERFACE
              && sel->lpVtbl->QueryInterface(sel, &iid_sel2, &p3) == S_OK && p3 == sel);
        if (p1) w->lpVtbl->Release(w);
        if (p3) sel->lpVtbl->Release(sel);
    }
    host.selchanges = 0;
    sel->lpVtbl->SetRange(sel, 3, 8);
    check("selection SetRange moves the text services' selection", get_sel().cpMin == 3 && get_sel().cpMax == 8
          && host.selchanges > 0);
    sel->lpVtbl->GetFlags(sel, &n);
    check("GetFlags: the active end is the end", !(n & tomSelStartActive));
    sel->lpVtbl->SetFlags(sel, n | tomSelStartActive);
    sel->lpVtbl->GetFlags(sel, &n);
    check("SetFlags(tomSelStartActive)", (n & tomSelStartActive) && ends_are(sel, 3, 8));
    sel->lpVtbl->Expand(sel, tomStory, 0);
    check("select all (Expand(tomStory)): not the final paragraph mark", ends_are(sel, 0, 14) && get_sel().cpMax == 14);
    sel->lpVtbl->GetCch(sel, &n);
    check("GetCch", n == 14);
    sel->lpVtbl->Collapse(sel, tomFalse);
    sel->lpVtbl->GetType(sel, &n);
    check("Collapse(false): the caret at the end", ends_are(sel, 14, 14) && n == tomSelectionIP);
    sel->lpVtbl->MoveLeft(sel, tomWord, 1, tomMove, 0);
    check("MoveLeft(tomWord)", ends_are(sel, 9, 9));
    sel->lpVtbl->MoveRight(sel, tomCharacter, 2, tomExtend, &delta);
    check("MoveRight(tomCharacter, 2, tomExtend)", ends_are(sel, 9, 11) && delta == 2);
    sel->lpVtbl->HomeKey(sel, tomLine, tomMove, 0);
    check("HomeKey", ends_are(sel, 0, 0));
    sel->lpVtbl->EndKey(sel, tomLine, tomMove, 0);
    check("EndKey", ends_are(sel, 14, 14));
    b = SysAllocString(L"!");
    sel->lpVtbl->TypeText(sel, b);
    SysFreeString(b);
    check("TypeText", text_is(L"Oh Howdy world!") && ends_are(sel, 15, 15));

    /* units, finding, case, deleting */
    r = range(d, 0, 0);
    check("Move(tomWord, 2)", r->lpVtbl->Move(r, tomWord, 2, &delta) == S_OK && delta == 2 && ends_are(r, 9, 9));
    r->lpVtbl->MoveEnd(r, tomCharacter, 5, 0);
    check("MoveEnd", range_text_is(r, L"world"));
    r->lpVtbl->ChangeCase(r, tomUpperCase);
    check("ChangeCase(tomUpperCase)", text_is(L"Oh Howdy WORLD!"));
    r->lpVtbl->SetRange(r, 0, 0);
    b = SysAllocString(L"howdy");
    check("FindText ignores case unless asked", r->lpVtbl->FindText(r, b, tomForward, 0, &n) == S_OK && n == 5
          && ends_are(r, 3, 8) && r->lpVtbl->FindText(r, b, tomForward, tomMatchCase, &n) == S_FALSE);
    SysFreeString(b);
    r->lpVtbl->Delete(r, tomCharacter, 0, 0);
    check("Delete", text_is(L"Oh  WORLD!"));
    r->lpVtbl->Release(r);

    /* undo and redo, and suspending them as XAML does around its own changes */
    check("Undo(1)", d->lpVtbl->Undo(d, 1, &n) == S_OK && n == 1 && text_is(L"Oh Howdy WORLD!"));
    check("Redo(1)", d->lpVtbl->Redo(d, 1, &n) == S_OK && text_is(L"Oh  WORLD!"));
    msg(EM_EMPTYUNDOBUFFER, 0, 0);
    d->lpVtbl->Undo(d, tomSuspend, 0);
    r = range(d, 0, 2);
    b = SysAllocString(L"Ah");
    r->lpVtbl->SetText(r, b);
    SysFreeString(b);
    d->lpVtbl->Undo(d, tomResume, 0);
    check("Undo(tomSuspend): changes are not recorded", text_is(L"Ah  WORLD!") && msg(EM_CANUNDO, 0, 0) == 0);
    r->lpVtbl->Release(r);
    d->lpVtbl->Freeze(d, &n);
    check("Freeze and Unfreeze count", n == 1 && d->lpVtbl->Unfreeze(d, &n) == S_OK && n == 0);

    /* points: GetPoint as TextBoxView finds the caret, RangeFromPoint2 back */
    r = range(d, 4, 4);
    r->lpVtbl->GetPoint(r, tomStart | tomClientCoord | tomAllowOffClient, &x, &y);
    r->lpVtbl->GetPoint(r, tomStart | tomClientCoord | tomAllowOffClient | 8 /* TA_BOTTOM */, &x2, &y2);
    check("GetPoint: top and bottom of the line", x > 5 && y == 0 && y2 > 10 && x2 == x);
    r->lpVtbl->Release(r);
    r = 0;
    d->lpVtbl->RangeFromPoint2(d, x + 1, y + 2, tomClientCoord, &r);
    check("RangeFromPoint2 finds the same place", r && ends_are(r, 4, 4));
    if (r) r->lpVtbl->Release(r);

    /* the clipboard */
    r = range(d, 4, 9);
    r->lpVtbl->Copy(r, 0);
    check("Copy", clipboard_is(L"WORLD"));
    r->lpVtbl->Collapse(r, tomStart);
    r->lpVtbl->Paste(r, 0, 0);
    check("Paste", text_is(L"Ah  WORLDWORLD!"));
    msg(EM_SETREADONLY, TRUE, 0);
    b = SysAllocString(L"x");
    r->lpVtbl->CanEdit(r, &n);
    check("read-only: SetText refused, CanEdit false", r->lpVtbl->SetText(r, b) == E_ACCESSDENIED && n == tomFalse);
    SysFreeString(b);
    msg(EM_SETREADONLY, FALSE, 0);
    r->lpVtbl->Release(r);

    /* paragraphs and lines */
    host.bits |= TXTBIT_MULTILINE;
    ts->lpVtbl->OnTxPropertyBitsChange(ts, TXTBIT_MULTILINE, host.bits);
    msg(WM_SETTEXT, 0, (LPARAM)L"one\r\ntwo");
    r = range(d, 5, 5);
    r->lpVtbl->GetIndex(r, tomParagraph, &n);
    check("GetIndex(tomParagraph)", n == 2);
    r->lpVtbl->Expand(r, tomParagraph, 0);
    check("Expand(tomParagraph): the last takes in the final mark", ends_are(r, 4, 8));
    r->lpVtbl->SetRange(r, 0, 0);
    r->lpVtbl->Move(r, tomLine, 1, 0);
    check("Move(tomLine)", ends_are(r, 4, 4));
    r->lpVtbl->Release(r);
    host.bits &= ~TXTBIT_MULTILINE;
    ts->lpVtbl->OnTxPropertyBitsChange(ts, TXTBIT_MULTILINE, host.bits);

    kept = range(d, 0, 1);
    sel->lpVtbl->Release(sel);
    w->lpVtbl->Release(w);
    d->lpVtbl->Release(d);
}

/* ---- TxDrawD2D ------------------------------------------------------------ */
typedef struct { float r, g, b, a; } COLORF_;
typedef struct { UINT32 type, format, alpha; float dpix, dpiy; UINT32 usage, level; } RTPROPS_;
typedef HRESULT (WINAPI *PD2D1CREATEFACTORY)(UINT32, REFIID, const void *, void **);
#define SLOT(p, i, type) ((type)((*(void ***)(p))[i]))

static int count_near(DWORD rgb, int tol)
{
    int i, n = 0;
    for (i = 0; i < BW * BH; i++) {
        DWORD p = bits_of[i];
        int dr = (int)((p >> 16) & 0xff) - (int)((rgb >> 16) & 0xff);
        int dg = (int)((p >> 8) & 0xff) - (int)((rgb >> 8) & 0xff);
        int db = (int)(p & 0xff) - (int)(rgb & 0xff);
        n += dr * dr <= tol * tol && dg * dg <= tol * tol && db * db <= tol * tol;
    }
    return n;
}

static HRESULT d2d_draw(void *rt, const COLORF_ *bg)
{
    RECTL rc = {0, 0, BW, BH};
    HRESULT hr;
    UINT64 t1, t2;
    SLOT(rt, 48, void (STDMETHODCALLTYPE *)(void *))(rt);                         /* BeginDraw */
    SLOT(rt, 47, void (STDMETHODCALLTYPE *)(void *, const COLORF_ *))(rt, bg);     /* Clear */
    hr = ts->lpVtbl->TxDrawD2D(ts, rt, &rc, 0, TXTVIEW_ACTIVE);
    SLOT(rt, 49, HRESULT (STDMETHODCALLTYPE *)(void *, UINT64 *, UINT64 *))(rt, &t1, &t2);   /* EndDraw */
    return hr;
}

static void drawd2d(HDC dc)
{
    static const IID iid_factory = {0x06152247, 0x6f50, 0x465a, {0x92, 0x45, 0x11, 0x8b, 0xfd, 0x3b, 0x60, 0x07}};
    HMODULE m = LoadLibraryA("d2d1.dll");
    PD2D1CREATEFACTORY fn = m ? (PD2D1CREATEFACTORY)GetProcAddress(m, "D2D1CreateFactory") : 0;
    void *f = 0, *rt = 0;
    RTPROPS_ props = {0, 87, 1, 0, 0, 0, 0};     /* B8G8R8A8, premultiplied */
    RECT rc = {0, 0, BW, BH};
    COLORF_ green = {0, 1, 0, 1};
    HRESULT hr;
    int dark, hl;
    if (fn) fn(0, &iid_factory, 0, &f);
    if (f) SLOT(f, 16, HRESULT (STDMETHODCALLTYPE *)(void *, const RTPROPS_ *, void **))(f, &props, &rt);   /* CreateDCRenderTarget */
    check("Direct2D: a DC render target", rt != 0);
    if (!rt) { if (f) ((IUnknown *)f)->lpVtbl->Release((IUnknown *)f); return; }
    SLOT(rt, 57, HRESULT (STDMETHODCALLTYPE *)(void *, HDC, const RECT *))(rt, dc, &rc);   /* BindDC */
    msg(WM_SETTEXT, 0, (LPARAM)L"Drawn text");
    set_sel(0, 5);
    hr = d2d_draw(rt, &green);
    dark = count_dark();
    hl = count_near(0x0078d7, 6);
    check("TxDrawD2D: the text and the selection's highlight", hr == S_OK && dark > 10 && hl > 50);
    check("TxDrawD2D opaque: the window colour over the background", count_near(0x00ff00, 8) == 0);
    host.back = TXTBACK_TRANSPARENT;
    hr = d2d_draw(rt, &green);
    check("TxDrawD2D transparent: the background stays", hr == S_OK && count_near(0x00ff00, 8) > BW * BH / 2);
    check("TxDrawD2D transparent: the text and highlight on it", count_dark() > 10 && count_near(0x0078d7, 6) > 50);
    host.back = TXTBACK_OPAQUE;
    set_sel(0, 0);
    ((IUnknown *)rt)->lpVtbl->Release((IUnknown *)rt);
    ((IUnknown *)f)->lpVtbl->Release((IUnknown *)f);
}

/* ---- the cut, copy and paste keys, offered to the host first ----------- */
static void ctrl_key(UINT vk)
{
    BYTE keys[256];
    GetKeyboardState(keys);
    keys[VK_CONTROL] = 0x80;
    SetKeyboardState(keys);
    key(vk);
    keys[VK_CONTROL] = 0;
    SetKeyboardState(keys);
}

static void requests(void)
{
    static const IID iid_doc2_ = NOVA_IID_ITextDocument2;
    ITextDocument2 *d = 0;
    ITextSelection2 *sel = 0;
    int p0 = host.requests[0], c0 = host.requests[1], x0 = host.requests[2];
    msg(WM_SETTEXT, 0, (LPARAM)L"one two");
    ctrl_key('A');
    check("Ctrl+A selects all", get_sel().cpMin == 0 && get_sel().cpMax == 7);
    set_sel(4, 7);
    ctrl_key('C');
    check("Ctrl+C: EN_COPYREQUESTED to the host, then the copy", host.requests[1] == c0 + 1 && clipboard_is(L"two"));
    set_sel(0, 4);
    ctrl_key('X');
    check("Ctrl+X: EN_CUTREQUESTED, then the cut", host.requests[2] == x0 + 1 && text_is(L"two") && clipboard_is(L"one "));
    set_sel(3, 3);
    ctrl_key('V');
    check("Ctrl+V: EN_PASTEREQUESTED, then the paste", host.requests[0] == p0 + 1 && text_is(L"twoone "));
    host.handle_requests = 1;
    ctrl_key('V');
    set_sel(0, 3);
    ctrl_key('X');
    check("a request the host handles is left to it", host.requests[0] == p0 + 2 && host.requests[2] == x0 + 2 && text_is(L"twoone "));
    /* (XAML's text boxes take the keys themselves and paste through the
     * TOM selection: the same requests) */
    ts->lpVtbl->QueryInterface(ts, &iid_doc2_, (void **)&d);
    if (d) d->lpVtbl->GetSelection2(d, &sel);
    if (sel) {
        sel->lpVtbl->Paste(sel, 0, 0);
        check("the selection's Paste: EN_PASTEREQUESTED, handled by the host", host.requests[0] == p0 + 3 && text_is(L"twoone "));
        host.handle_requests = 0;
        sel->lpVtbl->Copy(sel, 0);
        check("the selection's Copy: EN_COPYREQUESTED, then the copy", host.requests[1] == c0 + 2 && clipboard_is(L"two"));
        set_sel(0, 0);
        sel->lpVtbl->Paste(sel, 0, 0);
        check("the selection's Paste", host.requests[0] == p0 + 4 && text_is(L"twotwoone "));
        sel->lpVtbl->Release(sel);
    } else check("the TOM selection", 0);
    if (d) d->lpVtbl->Release(d);
    host.handle_requests = 0;
    msg(WM_PASTE, 0, 0);
    check("WM_PASTE is not a request", host.requests[0] == p0 + 4);
}

/* ---- the context menu: the host's OLE callback asked for it ------------- */
typedef struct { IRichEditOleCallback_ cb; LONG ref; int asked; WORD seltype; NOVA_GETCONTEXTMENUEX ex; } MENUCB;
static HRESULT STDMETHODCALLTYPE mcb_qi(IRichEditOleCallback_ *me, REFIID iid, void **out)
{
    static const IID iid_cb = NOVA_IID_IRichEditOleCallback;
    if (!memcmp(iid, &iid_unknown, sizeof(IID)) || !memcmp(iid, &iid_cb, sizeof(IID))) {
        *out = me;
        me->lpVtbl->AddRef(me);
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE mcb_addref(IRichEditOleCallback_ *me) { return (ULONG)++((MENUCB *)me)->ref; }
static ULONG STDMETHODCALLTYPE mcb_release(IRichEditOleCallback_ *me) { return (ULONG)--((MENUCB *)me)->ref; }
static HRESULT STDMETHODCALLTYPE mcb_menu(IRichEditOleCallback_ *me, WORD seltype, void *obj, CHARRANGE *chrg, HMENU *menu)
{
    MENUCB *m = (MENUCB *)me;
    (void)obj;
    m->asked++;
    m->seltype = seltype;
    if (seltype & GCM_MOUSEMENU) m->ex = *(NOVA_GETCONTEXTMENUEX *)chrg;
    else { memset(&m->ex, 0, sizeof m->ex); m->ex.chrg = *chrg; }
    *menu = 0;
    return S_OK;
}
/* (the text services ask only for the menu: the other slots are left empty) */
static const IRichEditOleCallback_Vtbl mcb_vtbl = {
    .QueryInterface = mcb_qi, .AddRef = mcb_addref, .Release = mcb_release, .GetContextMenu = mcb_menu };

static void context_menu(void)
{
    static MENUCB m = { { &mcb_vtbl }, 1 };
    NOVA_RE_MOUSEINPUT in;
    msg(WM_SETTEXT, 0, (LPARAM)L"one two");
    check("EM_SETOLECALLBACK", msg(EM_SETOLECALLBACK, 0, (LPARAM)&m.cb) == TRUE && m.ref == 2);
    /* (XAML's text boxes: the button's press, then its release, which
     * asks for the menu) */
    memset(&in, 0, sizeof in);
    in.msg = WM_RBUTTONDOWN;
    in.pt.x = 3;
    in.pt.y = 5;
    msg(NOVA_EM_MOUSEINPUT, 0, (LPARAM)&in);
    check("the right button's press moves the caret there", get_sel().cpMin == get_sel().cpMax);
    check("the press asks nothing", m.asked == 0);
    in.msg = WM_RBUTTONUP;
    check("its release asks the callback for the menu", msg_hr(NOVA_EM_MOUSEINPUT, 0, (LPARAM)&in) == S_OK && m.asked == 1);
    check("for the mouse, at its point, nothing selected", (m.seltype & GCM_MOUSEMENU) && (m.seltype & 0xFF) == SEL_EMPTY &&
          m.ex.pt.x == 3 && m.ex.pt.y == 5 && m.ex.chrg.cpMin == m.ex.chrg.cpMax);
    set_sel(4, 7);
    check("the menu key's release asks for it", msg_hr(WM_KEYUP, VK_APPS, 1) == S_OK && m.asked == 2);
    check("for the keyboard, with the selection", !(m.seltype & GCM_MOUSEMENU) && (m.seltype & 0xFF) == SEL_TEXT &&
          m.ex.chrg.cpMin == 4 && m.ex.chrg.cpMax == 7);
    check("another key's release is not handled", msg_hr(WM_KEYUP, 'A', 1) == S_FALSE && m.asked == 2);
    /* (a text box with a ContextFlyout turns Rich Edit's menu off) */
    msg(EM_SETTOUCHOPTIONS, RTO_DISABLECONTEXTMENU, TRUE);
    check("EM_SETTOUCHOPTIONS RTO_DISABLECONTEXTMENU", msg(EM_GETTOUCHOPTIONS, RTO_DISABLECONTEXTMENU, 0) == TRUE);
    check("then the menu key asks nothing", msg_hr(WM_KEYUP, VK_APPS, 1) == S_FALSE && m.asked == 2);
    msg(EM_SETTOUCHOPTIONS, RTO_DISABLECONTEXTMENU, FALSE);
    /* (what XAML's callback asks the document for its menu's items) */
    {
        static const IID iid_doc2_ = NOVA_IID_ITextDocument2;
        ITextDocument2 *d = 0;
        LONG copy = 0, undo = 0, redo = 1;
        ts->lpVtbl->QueryInterface(ts, &iid_doc2_, (void **)&d);
        if (d) {
            d->lpVtbl->GetProperty(d, 137, &copy);      /* tomCanCopy */
            d->lpVtbl->GetProperty(d, 139, &undo);      /* tomCanUndo */
            d->lpVtbl->GetProperty(d, 138, &redo);      /* tomCanRedo */
            d->lpVtbl->Release(d);
        }
        check("the document's tomCanCopy, tomCanUndo and tomCanRedo", copy == -1 && redo == 0 && (undo == -1 || undo == 0));
    }
    msg(EM_SETOLECALLBACK, 0, 0);
    check("EM_SETOLECALLBACK NULL lets the callback go", m.ref == 1);
    check("and nothing is asked", msg_hr(WM_KEYUP, VK_APPS, 1) == S_FALSE && m.asked == 2);
}

/* ---- the Windows Runtime document (a XAML RichEditBox's Document) -------- */
static HRESULT (WINAPI *p_hcreate)(const WCHAR *, UINT32, HSTRING *);
static const WCHAR *(WINAPI *p_hbuffer)(HSTRING, UINT32 *);
static HRESULT (WINAPI *p_hdelete)(HSTRING);

static int hstring_is(HSTRING h, const WCHAR *want)
{
    UINT32 n = 0;
    const WCHAR *p = h ? p_hbuffer(h, &n) : L"";
    int ok = n == wcslen(want) && !memcmp(p, want, n * sizeof(WCHAR));
    if (!ok) printf("  string is \"%.*ls\", want \"%ls\"\n", (int)n, p, want);
    if (h) p_hdelete(h);
    return ok;
}

/* (Microsoft.UI.Text's interfaces, by vtable slot: IInspectable's six, then theirs) */
#define WSLOT(p, i, type) SLOT(p, 6 + (i), type)
static void winrt(void)
{
    static const IID iid_wdoc = {0x1149d57d, 0x86a6, 0x59dd, {0x88, 0xd9, 0x19, 0x6f, 0x27, 0xbc, 0x5c, 0x85}};
    static const IID iid_wsel = {0x8f5e6cb1, 0x2b04, 0x589f, {0xbd, 0x24, 0x54, 0xe5, 0xcd, 0x8d, 0xd3, 0x99}};
    static const IID iid_wrange = {0x06d4abcf, 0x0c06, 0x5d12, {0xa7, 0x43, 0x85, 0x53, 0x7e, 0xfd, 0x09, 0xea}};
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    IUnknown *doc = 0, *r = 0, *sel = 0, *sel2 = 0, *selr = 0;
    HSTRING h = 0;
    INT32 n = 0, d = 0;
    BYTE b = 0;
    p_hcreate = (void *)GetProcAddress(ole, "WindowsCreateString");
    p_hbuffer = (void *)GetProcAddress(ole, "WindowsGetStringRawBuffer");
    p_hdelete = (void *)GetProcAddress(ole, "WindowsDeleteString");
    if (!p_hcreate || !p_hbuffer || !p_hdelete) { check("WinRT: ole32's strings", 0); return; }
    check("WinRT: QueryInterface(Microsoft.UI.Text.ITextDocument), as XAML's RichEditBox does",
          ts->lpVtbl->QueryInterface(ts, &iid_wdoc, (void **)&doc) == S_OK && doc);
    if (!doc) return;
    msg(WM_SETTEXT, 0, (LPARAM)L"");
    p_hcreate(L"Rich text", 9, &h);
    check("ITextDocument.SetText", WSLOT(doc, 25, HRESULT (STDMETHODCALLTYPE *)(void *, INT32, HSTRING))(doc, 0, h) == S_OK &&
          text_is(L"Rich text"));
    p_hdelete(h);
    h = 0;
    check("ITextDocument.GetText", WSLOT(doc, 19, HRESULT (STDMETHODCALLTYPE *)(void *, INT32, HSTRING *))(doc, 0, &h) == S_OK &&
          hstring_is(h, L"Rich text"));
    check("ITextDocument.GetRange(0, 2)", WSLOT(doc, 17, HRESULT (STDMETHODCALLTYPE *)(void *, INT32, INT32, void **))(doc, 0, 2, (void **)&r) == S_OK && r);
    if (r) {
        WSLOT(r, 17, HRESULT (STDMETHODCALLTYPE *)(void *, INT32 *))(r, &n);                   /* get_StoryLength */
        check("ITextRange.StoryLength: the text and the final paragraph mark", n == 10);
        h = 0;
        WSLOT(r, 18, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING *))(r, &h);                 /* get_Text */
        check("ITextRange.Text", hstring_is(h, L"Ri"));
        WSLOT(r, 41, HRESULT (STDMETHODCALLTYPE *)(void *, INT32, INT32, INT32 *))(r, 1, 1, &d);   /* MoveEnd(Word, 1) */
        WSLOT(r, 10, HRESULT (STDMETHODCALLTYPE *)(void *, INT32 *))(r, &n);                   /* get_Length */
        check("ITextRange.MoveEnd(Word): TextRangeUnit is TOM's unit", d == 1 && n == 5);
        r->lpVtbl->Release(r);
    }
    check("ITextDocument.Selection", WSLOT(doc, 4, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(doc, (void **)&sel) == S_OK && sel);
    if (sel) {
        /* (Selection is an ITextSelection, whose range is its ITextRange) */
        sel->lpVtbl->QueryInterface(sel, &iid_wsel, (void **)&sel2);
        sel->lpVtbl->QueryInterface(sel, &iid_wrange, (void **)&selr);
        check("the selection: ITextSelection and ITextRange", sel2 == sel && selr != 0);
        if (selr) {
            WSLOT(selr, 48, HRESULT (STDMETHODCALLTYPE *)(void *, INT32, INT32))(selr, 0, 4);  /* SetRange */
            selr->lpVtbl->Release(selr);
        }
        check("ITextRange.SetRange on the selection selects", get_sel().cpMin == 0 && get_sel().cpMax == 4);
        WSLOT(doc, 7, HRESULT (STDMETHODCALLTYPE *)(void *, BYTE *))(doc, &b);
        check("ITextDocument.CanCopy with a selection", b == 1);
        if (sel2) {
            p_hcreate(L"Poor", 4, &h);
            WSLOT(sel2, 9, HRESULT (STDMETHODCALLTYPE *)(void *, HSTRING))(sel2, h);          /* TypeText */
            p_hdelete(h);
            check("ITextSelection.TypeText", text_is(L"Poor text"));
            sel2->lpVtbl->Release(sel2);
        }
        sel->lpVtbl->Release(sel);
    }
    WSLOT(doc, 10, HRESULT (STDMETHODCALLTYPE *)(void *, BYTE *))(doc, &b);
    check("ITextDocument.CanUndo", b == 1);
    WSLOT(doc, 26, HRESULT (STDMETHODCALLTYPE *)(void *))(doc);                               /* Undo */
    check("ITextDocument.Undo", text_is(L"Rich text"));
    doc->lpVtbl->Release(doc);
    msg(WM_SETTEXT, 0, (LPARAM)L"");
}

/* ---- rich text's character formats (a RichEditBox's) ------------------- */
static int count_red(void)
{
    int i, n = 0;
    for (i = 0; i < BW * BH; i++) {
        DWORD p = bits_of[i];
        n += ((p >> 16) & 0xff) > 150 && ((p >> 8) & 0xff) < 100 && (p & 0xff) < 100;
    }
    return n;
}
static DWORD sel_effects(LONG a, LONG b, DWORD *mask, COLORREF *color)
{
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cf.cbSize = sizeof(cf);
    set_sel(a, b);
    msg(EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    if (mask) *mask = cf.cf.dwMask;
    if (color) *color = cf.cf.crTextColor;
    return cf.cf.dwEffects;
}
static LRESULT set_format(LONG a, LONG b, DWORD mask, DWORD effects, COLORREF color)
{
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cf.cbSize = sizeof(cf);
    cf.cf.dwMask = mask;
    cf.cf.dwEffects = effects;
    cf.cf.crTextColor = color;
    set_sel(a, b);
    return msg(EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}
#define FSLOT(p, i, type) SLOT(p, 7 + (i), type)  /* (ITextFont's methods after IDispatch's) */
static void formatting(HDC dc)
{
    static const IID iid_doc2_ = NOVA_IID_ITextDocument2;
    static const IID iid_wdoc = {0x1149d57d, 0x86a6, 0x59dd, {0x88, 0xd9, 0x19, 0x6f, 0x27, 0xbc, 0x5c, 0x85}};
    ITextDocument2 *d = 0;
    ITextRange2 *r = 0;
    IUnknown *font = 0, *dup = 0, *doc = 0, *wr = 0, *wcf = 0;
    DWORD mask = 0, e;
    COLORREF c = 0;
    LONG v = 0;
    INT32 fx = -1;
    int dark, under;
    struct { BYTE A, R, G, B; } col;

    host.bits |= TXTBIT_RICHTEXT;
    ts->lpVtbl->OnTxPropertyBitsChange(ts, TXTBIT_RICHTEXT, host.bits);
    msg(WM_SETTEXT, 0, (LPARAM)L"plain bold red");
    set_sel(0, 0);
    draw(dc);
    dark = count_dark();
    check("rich text: no red before a colour is set", count_red() == 0);
    check("EM_SETCHARFORMAT(SCF_SELECTION) bold on \"bold\"", set_format(6, 10, CFM_BOLD, CFE_BOLD, 0) != 0);
    e = sel_effects(6, 10, &mask, 0);
    check("EM_GETCHARFORMAT(SCF_SELECTION): \"bold\" is bold", (mask & CFM_BOLD) && (e & CFE_BOLD));
    e = sel_effects(0, 5, &mask, 0);
    check("... and \"plain\" is not", (mask & CFM_BOLD) && !(e & CFE_BOLD));
    sel_effects(0, 10, &mask, 0);
    check("... a selection over both: bold undefined (CFM_BOLD clear)", !(mask & CFM_BOLD) && (mask & CFM_ITALIC));
    set_sel(0, 0);
    draw(dc);
    check("bold text is drawn heavier", count_dark() > dark);
    set_format(11, 14, CFM_COLOR, 0, RGB(255, 0, 0));
    set_sel(0, 0);
    draw(dc);
    check("a run's colour: \"red\" is drawn red", count_red() > 10);
    e = sel_effects(11, 14, &mask, &c);
    check("... and EM_GETCHARFORMAT gives it", (mask & CFM_COLOR) && !(e & CFE_AUTOCOLOR) && c == RGB(255, 0, 0));
    dark = count_dark();
    set_format(0, 5, CFM_UNDERLINE, CFE_UNDERLINE, 0);
    set_sel(0, 0);
    draw(dc);
    under = count_dark();
    check("an underline is drawn under \"plain\"", under > dark);
    set_format(0, 5, CFM_UNDERLINE | CFM_STRIKEOUT, CFE_STRIKEOUT, 0);
    set_sel(0, 0);
    draw(dc);
    check("strikethrough in place of the underline", (sel_effects(0, 5, 0, 0) & (CFE_UNDERLINE | CFE_STRIKEOUT)) == CFE_STRIKEOUT);
    /* typing takes the format of the character before it, or the caret's */
    set_sel(10, 10);
    type(L"X");
    check("typed after \"bold\": bold", text_is(L"plain boldX red") && (sel_effects(10, 11, 0, 0) & CFE_BOLD));
    set_format(0, 0, CFM_ITALIC, CFE_ITALIC, 0);
    type(L"I");
    check("the caret's format (EM_SETCHARFORMAT, no selection): the typed character is italic",
          text_is(L"Iplain boldX red") && (sel_effects(0, 1, 0, 0) & CFE_ITALIC) && !(sel_effects(1, 2, 0, 0) & CFE_ITALIC));
    /* the formatting keys */
    set_sel(1, 6);
    ctrl_key('B');
    check("Ctrl+B: bold", (sel_effects(1, 6, &mask, 0) & CFE_BOLD) && (mask & CFM_BOLD));
    set_sel(1, 6);
    ctrl_key('B');
    check("Ctrl+B again: not bold", !(sel_effects(1, 6, 0, 0) & CFE_BOLD));
    set_sel(1, 6);
    ctrl_key('U');
    check("Ctrl+U: underline", sel_effects(1, 6, 0, 0) & CFE_UNDERLINE);
    msg(NOVA_EM_SETFORMATACCELERATORS, 0, CFE_ITALIC | CFE_UNDERLINE);   /* (DisabledFormattingAccelerators.Bold) */
    set_sel(1, 6);
    ctrl_key('B');
    check("a formatting key turned off does nothing", !(sel_effects(1, 6, 0, 0) & CFE_BOLD));
    msg(NOVA_EM_SETFORMATACCELERATORS, 0, CFE_BOLD | CFE_ITALIC | CFE_UNDERLINE);
    ctrl_key('Z');
    check("Ctrl+Z undoes a format change", !(sel_effects(1, 6, 0, 0) & CFE_UNDERLINE) && text_is(L"Iplain boldX red"));
    /* the Text Object Model's font: live over its range */
    ts->lpVtbl->QueryInterface(ts, &iid_doc2_, (void **)&d);
    if (d) d->lpVtbl->Range2(d, 7, 11, &r);       /* "bold" */
    if (r) r->lpVtbl->GetFont(r, (void **)&font);
    check("ITextRange.GetFont", font != 0);
    if (font) {
        FSLOT(font, 13, HRESULT (STDMETHODCALLTYPE *)(void *, LONG *))(font, &v);          /* GetBold */
        check("ITextFont.GetBold: tomTrue", v == tomTrue);
        r->lpVtbl->SetStart(r, 0);
        FSLOT(font, 13, HRESULT (STDMETHODCALLTYPE *)(void *, LONG *))(font, &v);
        check("... live: the range moved over plain text too, tomUndefined", v == tomUndefined);
        FSLOT(font, 24, HRESULT (STDMETHODCALLTYPE *)(void *, LONG))(font, tomTrue);       /* SetItalic */
        check("ITextFont.SetItalic formats the range", (sel_effects(0, 11, &mask, 0) & CFE_ITALIC) && (mask & CFM_ITALIC));
        FSLOT(font, 0, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(font, (void **)&dup);   /* GetDuplicate */
        if (dup) {
            ITextRange2 *r2 = 0;
            FSLOT(dup, 18, HRESULT (STDMETHODCALLTYPE *)(void *, LONG))(dup, RGB(0, 0, 255));  /* SetForeColor */
            sel_effects(0, 11, 0, &c);
            check("a duplicate font is its own: the range keeps its colour", c != RGB(0, 0, 255));
            d->lpVtbl->Range2(d, 13, 16, &r2);
            if (r2) {
                r2->lpVtbl->SetFont(r2, dup);
                e = sel_effects(13, 16, &mask, &c);
                check("ITextRange.SetFont applies a duplicate's values", (e & CFE_ITALIC) && c == RGB(0, 0, 255));
                r2->lpVtbl->Release(r2);
            }
            dup->lpVtbl->Release(dup);
        } else check("ITextFont.GetDuplicate", 0);
        font->lpVtbl->Release(font);
    }
    if (r) r->lpVtbl->Release(r);
    if (d) d->lpVtbl->Release(d);
    /* the Windows Runtime's: a RichEditBox's Document.GetRange(..).CharacterFormat */
    ts->lpVtbl->QueryInterface(ts, &iid_wdoc, (void **)&doc);
    if (doc) WSLOT(doc, 17, HRESULT (STDMETHODCALLTYPE *)(void *, INT32, INT32, void **))(doc, 1, 6, (void **)&wr);
    if (wr) WSLOT(wr, 2, HRESULT (STDMETHODCALLTYPE *)(void *, void **))(wr, (void **)&wcf);   /* get_CharacterFormat */
    check("ITextRange.CharacterFormat", wcf != 0);
    if (wcf) {
        WSLOT(wcf, 5, HRESULT (STDMETHODCALLTYPE *)(void *, INT32))(wcf, 1);                    /* put_Bold(On) */
        WSLOT(wcf, 4, HRESULT (STDMETHODCALLTYPE *)(void *, INT32 *))(wcf, &fx);                /* get_Bold */
        check("ITextCharacterFormat.Bold = On formats the range", fx == 1 && (sel_effects(1, 6, 0, 0) & CFE_BOLD));
        col.A = 255; col.R = 0; col.G = 128; col.B = 0;
        WSLOT(wcf, 11, HRESULT (STDMETHODCALLTYPE *)(void *, typeof(col)))(wcf, col);         /* put_ForegroundColor */
        memset(&col, 0, sizeof(col));
        WSLOT(wcf, 10, HRESULT (STDMETHODCALLTYPE *)(void *, typeof(col) *))(wcf, &col);       /* get_ForegroundColor */
        sel_effects(1, 6, 0, &c);
        check("ITextCharacterFormat.ForegroundColor", col.A == 255 && col.G == 128 && c == RGB(0, 128, 0));
        WSLOT(wcf, 43, HRESULT (STDMETHODCALLTYPE *)(void *, INT32 *))(wcf, &fx);               /* get_Underline */
        check("ITextCharacterFormat.Underline: None", fx == 1);
        wcf->lpVtbl->Release(wcf);
    }
    if (wr) wr->lpVtbl->Release(wr);
    if (doc) doc->lpVtbl->Release(doc);
    /* plain text has one format: the selection's change is the default's */
    host.bits &= ~TXTBIT_RICHTEXT;
    ts->lpVtbl->OnTxPropertyBitsChange(ts, TXTBIT_RICHTEXT, host.bits);
    msg(WM_SETTEXT, 0, (LPARAM)L"plain");
    set_format(0, 2, CFM_BOLD, CFE_BOLD, 0);
    check("plain text: a selection's format is the whole text's", sel_effects(3, 5, 0, 0) & CFE_BOLD);
    set_format(0, 5, CFM_BOLD, 0, 0);
    msg(WM_SETTEXT, 0, (LPARAM)L"");
}

int main(void)
{
    HMODULE m = LoadLibraryW(L"WinUIEdit.dll");
    PCREATETEXTSERVICES create_fn;
    PSHUTDOWNTEXTSERVICES shutdown_fn;
    IUnknown *unk = 0, *unk2 = 0;
    ITextServices2 *ts1 = 0;
    HDC dc = CreateCompatibleDC(0);
    HBITMAP bmp = make_bitmap(dc), oldbmp;

    printf("textservtest: the windowless Rich Edit (%d-bit)\n", (int)sizeof(void *) * 8);
    check("WinUIEdit.dll loads", m != 0);
    if (!m) goto done;
    exports(m, "WinUIEdit.dll");
    create_fn = (PCREATETEXTSERVICES)GetProcAddress(m, "CreateTextServices");
    shutdown_fn = (PSHUTDOWNTEXTSERVICES)GetProcAddress(m, "ShutdownTextServices");
    if (!create_fn || !shutdown_fn || !bmp) goto done;
    oldbmp = SelectObject(dc, bmp);

    host_init(&host, 0, BW, dc);
    ts = create(create_fn, &host, &unk);
    check("CreateTextServices, then ITextServices2", ts != 0);
    if (!ts) goto done;
    check("CreateTextServices does not AddRef the host", host.ref == 1);
    ts->lpVtbl->QueryInterface(ts, &iid_unknown, (void **)&unk2);
    ts->lpVtbl->QueryInterface(ts, &iid_ts, (void **)&ts1);
    check("QueryInterface: IUnknown and ITextServices", unk2 == unk && ts1 == ts);
    if (unk2) unk2->lpVtbl->Release(unk2);
    if (ts1) ts1->lpVtbl->Release(ts1);
    ts->lpVtbl->OnTxInPlaceActivate(ts, &host.client);

    typing();
    selection_and_clipboard();
    limits();
    mouse();
    multiline();
    drawing(dc);
    password(dc);
    tom(m);
    requests();
    context_menu();
    winrt();
    formatting(dc);
    drawd2d(dc);
    others();

    check("ShutdownTextServices", shutdown_fn(unk) == S_OK);
    ts->lpVtbl->Release(ts);
    check("the last Release", unk->lpVtbl->Release(unk) == 0);
    if (kept) {
        LONG cp = 0;
        check("a range kept after the text services: CO_E_RELEASED", kept->lpVtbl->GetStart(kept, &cp) == (HRESULT)0x800401ff);
        kept->lpVtbl->Release(kept);
    }
    check("the host was never AddRef'd for good", host.ref == 1);
    SelectObject(dc, oldbmp);
done:
    printf("textservtest: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}
