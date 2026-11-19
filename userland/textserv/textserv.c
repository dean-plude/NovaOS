/*
 * textserv.c — the windowless Rich Edit: CreateTextServices and its
 * ITextServices2 object, built into WinUIEdit.dll (which XAML's TextBox,
 * PasswordBox and RichEditBox load), msftedit.dll and riched20.dll.
 *
 * The program hosting the text implements ITextHost (novatextserv.h): it
 * owns the window, the caret, the clipboard's owner window, the colours
 * and the default character format, and draws when the text services ask.
 * The text services keep the text (paragraphs end in '\r', as Rich Edit's
 * do), the selection, undo and redo, and answer the Rich Edit messages
 * the host passes on with TxSendMessage: typing (WM_CHAR), the editing
 * keys (WM_KEYDOWN: arrows, Home, End, Delete, with Shift extending the
 * selection and Ctrl moving by words; Ctrl+A, C, X, V, Z and Y), the mouse
 * (click, Shift+click, drag and double-click to select a word), the
 * clipboard (WM_COPY, WM_CUT, WM_PASTE, WM_CLEAR; a password box refuses
 * to copy or cut), and the text and selection messages (WM_SETTEXT,
 * WM_GETTEXT, EM_GETTEXTEX, EM_SETTEXTEX, EM_EXSETSEL, EM_REPLACESEL,
 * EM_GETTEXTRANGE, EM_POSFROMCHAR, EM_CHARFROMPOS, line messages, limits,
 * read-only, the event mask, modify flag and undo).  Changes are reported
 * to the host by TxNotify (EN_CHANGE, EN_SELCHANGE, EN_MAXTEXT) as its
 * event mask asks, and a cut, copy or paste key is offered to the host
 * first (WinUIEdit's EN_CUTREQUESTED, EN_COPYREQUESTED, EN_PASTEREQUESTED,
 * which XAML's text boxes answer with their clipboard events), and drawn by TxDraw on the host's device context in
 * the host's character format: the text, or the password character for
 * each character, with the selection highlighted, single-line text
 * scrolled to keep the caret in view and multi-line text word-wrapped.
 *
 * TxDrawD2D draws the same onto a Direct2D render target (XAML's text
 * boxes draw that way): with DirectWrite glyph runs in the face the host's
 * IProvideFontInfo names, when it has one (XAML's TextServicesHost:
 * dwtext.c), else through a bitmap.  XAML's pointer input arrives as
 * EM_MOUSEINPUT.  The Text Object Model over the same text
 * (ITextDocument2 by QueryInterface, IRichEditOle by EM_GETOLEINTERFACE,
 * its ranges and selection) is tom.c, its format objects tomfmt.c, and
 * its Windows Runtime face (Microsoft.UI.Text's ITextDocument, a XAML
 * RichEditBox's Document) winrtdoc.c.  Plain
 * text in one character format.  Written from the Rich Edit and text
 * services documentation.
 */
#include "tsint.h"

int _fltused = 0x9875;                            /* floating point in use (Direct2D's rectangles, the tab stop) */

GDIAPI BOOL GetTextExtentExPointW(HDC dc, LPCWSTR s, int n, int max, LPINT fit, LPINT dx, LPSIZE size);

/* the IIDs the three DLLs export as data (XAML checks them by GetProcAddress) */
__declspec(dllexport) const IID IID_ITextServices  = NOVA_IID_ITextServices;
__declspec(dllexport) const IID IID_ITextServices2 = NOVA_IID_ITextServices2;
__declspec(dllexport) const IID IID_ITextHost      = NOVA_IID_ITextHost;
__declspec(dllexport) const IID IID_ITextHost2     = NOVA_IID_ITextHost2;
__declspec(dllexport) const IID IID_ITextDocument2 = NOVA_IID_ITextDocument2;
__declspec(dllexport) const IID IID_IRicheditWindowlessAccessibility = NOVA_IID_IRicheditWindowlessAccessibility;
__declspec(dllexport) const IID IID_IRichEditOle   = NOVA_IID_IRichEditOle;
__declspec(dllexport) const IID IID_IRichEditOleCallback = NOVA_IID_IRichEditOleCallback;
static const IID iid_unknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

/* ---- memory ------------------------------------------------------------- */
void *alloc(SIZE_T n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1); }
void release(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

static BOOL reserve(TS *t, LONG n)
{
    WCHAR *b;
    LONG cap;
    if (n + 1 <= t->cap) return TRUE;
    cap = t->cap ? t->cap : 64;
    while (cap < n + 1) cap *= 2;
    if (!(b = alloc(cap * sizeof(WCHAR)))) return FALSE;
    if (t->text) { memcpy(b, t->text, t->len * sizeof(WCHAR)); release(t->text); }
    t->text = b;
    t->cap = cap;
    return TRUE;
}

/* ---- the host ----------------------------------------------------------- */

static void notify(TS *t, DWORD code, void *data)
{
    if (t->host) HOST(t)->TxNotify(t->host, code, data);
}

static void refresh_bits(TS *t)
{
    DWORD b = 0, max = 0;
    WCHAR ch = L'*';
    if (!t->host) return;
    if (HOST(t)->TxGetPropertyBits(t->host, 0xffffffff, &b) == S_OK) t->bits = b;
    if (HOST(t)->TxGetMaxLength(t->host, &max) == S_OK && max) t->maxlen = max;
    if ((t->bits & TXTBIT_USEPASSWORD) && HOST(t)->TxGetPasswordChar(t->host, &ch) == S_OK && ch) t->pwch = ch;
    t->dirty = TRUE;
}

static COLORREF syscolor(TS *t, int index)
{
    return t->host ? HOST(t)->TxGetSysColor(t->host, index) : GetSysColor(index);
}

/* The default character format: the host's, until EM_SETCHARFORMAT changes it */
static void refresh_format(TS *t)
{
    const CHARFORMATW *cf = 0;
    LOGFONTW lf;
    if (t->host && HOST(t)->TxGetCharFormat(t->host, &cf) == S_OK && cf) {
        UINT n = cf->cbSize < sizeof(t->cf) ? cf->cbSize : sizeof(t->cf);
        memset(&t->cf, 0, sizeof(t->cf));
        memcpy(&t->cf, cf, n >= sizeof(CHARFORMATW) ? n : sizeof(CHARFORMATW));
    }
    t->cf.cf.cbSize = sizeof(t->cf);
    if (!t->cf.cf.szFaceName[0]) memcpy(t->cf.cf.szFaceName, L"Segoe UI", 9 * sizeof(WCHAR));
    if (!t->cf.cf.yHeight) t->cf.cf.yHeight = 180;          /* 9 points */
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -(t->cf.cf.yHeight * 96 + 720) / 1440;   /* twips at 96 dpi */
    lf.lfWeight = (t->cf.cf.dwEffects & CFE_BOLD) ? FW_BOLD : FW_NORMAL;
    if (t->cf.wWeight && t->cf.cf.cbSize >= sizeof(CHARFORMAT2W)) lf.lfWeight = t->cf.wWeight;
    lf.lfItalic = (t->cf.cf.dwEffects & CFE_ITALIC) != 0;
    lf.lfUnderline = (t->cf.cf.dwEffects & CFE_UNDERLINE) != 0;
    lf.lfCharSet = DEFAULT_CHARSET;
    memcpy(lf.lfFaceName, t->cf.cf.szFaceName, sizeof(lf.lfFaceName));
    lf.lfFaceName[31] = 0;
    if (t->font) DeleteObject(t->font);
    t->font = CreateFontIndirectW(&lf);
    t->lineh = 0;
    t->dirty = TRUE;
    dw_refresh(t);                                /* (the host's DirectWrite face, when it gives one) */
}

/* A device context to measure with: TxDraw's, the host's, or our own */
static HDC get_dc(TS *t, BOOL *host)
{
    HDC dc = 0;
    *host = FALSE;
    if (t->drawdc) dc = t->drawdc;
    else if (t->host && (dc = HOST(t)->TxGetDC(t->host))) *host = TRUE;
    else {
        if (!t->memdc) t->memdc = CreateCompatibleDC(0);
        dc = t->memdc;
    }
    return dc;
}

static void put_dc(TS *t, HDC dc, BOOL host)
{
    if (host) HOST(t)->TxReleaseDC(t->host, dc);
}

/* ---- layout ------------------------------------------------------------- */
static WCHAR shown(TS *t, LONG i)
{
    return (t->bits & TXTBIT_USEPASSWORD) ? t->pwch : t->text[i];
}

/* the widths of [start, start+n) as shown, cumulative, into dx */
static void measure(TS *t, HDC dc, LONG start, LONG n, int *dx, SIZE *sz)
{
    WCHAR stackbuf[256], *s = stackbuf;
    LONG i;
    SIZE dummy;
    if (!sz) sz = &dummy;
    sz->cx = sz->cy = 0;
    if (n <= 0) return;
    if (n > 256) s = alloc(n * sizeof(WCHAR));
    for (i = 0; i < n; i++) s[i] = shown(t, start + i) == L'\t' ? L' ' : shown(t, start + i);
    if (dw_measure(t, s, n, dx, sz)) {            /* (the host's DirectWrite face: dwtext.c) */
        if (s != stackbuf) release(s);
        return;
    }
    if (!GetTextExtentExPointW(dc, s, (int)n, 0, 0, dx, sz) && dx) {
        int x = 0;                                /* (fallback: one at a time) */
        for (i = 0; i < n; i++) { SIZE c; GetTextExtentPoint32W(dc, s + i, 1, &c); x += c.cx; dx[i] = x; }
        sz->cx = x;
    }
    if (s != stackbuf) release(s);
}

static void add_line(TS *t, LONG start, LONG len, LONG width)
{
    if (t->nlines == t->linecap) {
        int cap = t->linecap ? t->linecap * 2 : 16;
        LINE *l = alloc(cap * sizeof(LINE));
        if (!l) return;
        if (t->lines) { memcpy(l, t->lines, t->nlines * sizeof(LINE)); release(t->lines); }
        t->lines = l;
        t->linecap = cap;
    }
    t->lines[t->nlines].start = start;
    t->lines[t->nlines].len = len;
    t->lines[t->nlines].width = width;
    t->nlines++;
}

/* Breaks the text into lines: at each '\r', and (word wrap) where a line
 * would pass @width, after the last space that fits */
static void layout(TS *t, HDC dc, LONG width)
{
    LONG p = 0;
    HGDIOBJ old = SelectObject(dc, t->font);
    BOOL wrap = (t->bits & (TXTBIT_MULTILINE | TXTBIT_WORDWRAP)) == (TXTBIT_MULTILINE | TXTBIT_WORDWRAP) && width > 0;
    if (!t->lineh) {
        TEXTMETRICW tm;
        memset(&tm, 0, sizeof(tm));
        GetTextMetricsW(dc, &tm);
        t->lineh = tm.tmHeight > 0 ? tm.tmHeight : 16;
        t->ascent = tm.tmAscent > 0 ? tm.tmAscent : 12;
    }
    t->nlines = 0;
    for (;;) {
        LONG e = p, n;
        while (e < t->len && t->text[e] != L'\r') e++;
        n = e - p;
        if (n == 0) add_line(t, p, 0, 0);
        else {
            int *dx = alloc(n * sizeof(int));
            LONG s = p;
            SIZE sz;
            measure(t, dc, p, n, dx, &sz);
            while (s < e) {
                LONG base = s > p ? dx[s - p - 1] : 0, k = s, brk = -1;
                if (!wrap) { add_line(t, s, e - s, dx[n - 1] - base); break; }
                while (k < e && dx[k - p] - base <= width) {
                    if (t->text[k] == L' ') brk = k + 1;
                    k++;
                }
                if (k == e) { add_line(t, s, e - s, dx[n - 1] - base); break; }
                if (brk <= s) brk = k > s ? k : s + 1;   /* no space: break the word */
                add_line(t, s, brk - s, dx[brk - p - 1] - base);
                s = brk;
            }
            release(dx);
        }
        if (e >= t->len) break;
        p = e + 1;
    }
    SelectObject(dc, old);
    t->layoutw = wrap ? width : 0;
    t->dirty = FALSE;
}

void client_rect(TS *t, RECT *rc)
{
    RECT inset;
    if (t->host && HOST(t)->TxGetClientRect(t->host, rc) == S_OK) t->client = *rc;
    else *rc = t->client;
    if (t->host && HOST(t)->TxGetViewInset(t->host, &inset) == S_OK) {   /* (HIMETRIC) */
        rc->left += inset.left * 96 / 2540;
        rc->top += inset.top * 96 / 2540;
        rc->right -= inset.right * 96 / 2540;
        rc->bottom -= inset.bottom * 96 / 2540;
    }
}

void ensure_layout(TS *t)
{
    RECT rc;
    LONG w;
    client_rect(t, &rc);
    w = rc.right - rc.left;
    if (t->dirty || !t->nlines || ((t->bits & TXTBIT_WORDWRAP) && (t->bits & TXTBIT_MULTILINE) && w != t->layoutw)) {
        BOOL host;
        HDC dc = get_dc(t, &host);
        layout(t, dc, w);
        put_dc(t, dc, host);
    }
}

int line_of(TS *t, LONG cp)
{
    int i;
    for (i = 0; i + 1 < t->nlines; i++)
        if (cp < t->lines[i + 1].start) return i;
    return t->nlines - 1;
}

/* x of character position @cp within its line (from the line's start) */
LONG x_of(TS *t, LONG cp)
{
    int l = line_of(t, cp);
    LONG n = cp - t->lines[l].start, x = 0;
    if (n > 0) {
        BOOL host;
        HDC dc = get_dc(t, &host);
        HGDIOBJ old = SelectObject(dc, t->font);
        SIZE sz;
        measure(t, dc, t->lines[l].start, n, 0, &sz);
        x = sz.cx;
        SelectObject(dc, old);
        put_dc(t, dc, host);
    }
    return x;
}

/* the character position on line @l nearest x (from the line's start) */
LONG cp_at_x(TS *t, int l, LONG x)
{
    LINE *ln = &t->lines[l];
    LONG i, cp = ln->start + ln->len;
    BOOL host;
    HDC dc;
    HGDIOBJ old;
    int *dx;
    if (x <= 0 || !ln->len) return ln->start;
    if (!(dx = alloc(ln->len * sizeof(int)))) return ln->start;
    dc = get_dc(t, &host);
    old = SelectObject(dc, t->font);
    measure(t, dc, ln->start, ln->len, dx, 0);
    SelectObject(dc, old);
    put_dc(t, dc, host);
    for (i = 0; i < ln->len; i++) {
        LONG left = i ? dx[i - 1] : 0;
        if (x < (left + dx[i]) / 2) { cp = ln->start + i; break; }
    }
    release(dx);
    /* a wrapped line's last position is the next line's first: stay on this one */
    if (cp == ln->start + ln->len && l + 1 < t->nlines && t->lines[l + 1].start == cp && cp > ln->start
        && t->text[cp - 1] == L' ')
        cp--;
    return cp;
}

/* the character position at client point (x, y) */
LONG hit(TS *t, LONG x, LONG y)
{
    RECT rc;
    int l;
    ensure_layout(t);
    client_rect(t, &rc);
    l = (y - rc.top + t->scrolly) / (t->lineh ? t->lineh : 16);
    if (l < 0) l = 0;
    if (l >= t->nlines) l = t->nlines - 1;
    return cp_at_x(t, l, x - rc.left + t->scrollx);
}

/* ---- the caret and the view --------------------------------------------- */
static void caret_point(TS *t, POINT *pt)
{
    RECT rc;
    ensure_layout(t);
    client_rect(t, &rc);
    pt->x = rc.left + x_of(t, t->active) - t->scrollx;
    pt->y = rc.top + line_of(t, t->active) * t->lineh - t->scrolly;
}

/* scrolls so character position @cp is inside the client area */
void scroll_to(TS *t, LONG cp)
{
    RECT rc;
    LONG x, y, w, h;
    ensure_layout(t);
    client_rect(t, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    x = x_of(t, cp);
    y = line_of(t, cp) * t->lineh;
    if (!(t->bits & TXTBIT_MULTILINE) || !(t->bits & TXTBIT_WORDWRAP)) {
        if (x - t->scrollx > w - 1) t->scrollx = x - w + 1;
        if (x < t->scrollx) t->scrollx = x;
    } else t->scrollx = 0;
    if (y + t->lineh - t->scrolly > h) t->scrolly = y + t->lineh - h;
    if (y < t->scrolly) t->scrolly = y;
    if (t->scrollx < 0) t->scrollx = 0;
    if (t->scrolly < 0) t->scrolly = 0;
}

static void scroll_to_caret(TS *t) { scroll_to(t, t->active); }

static void update_caret(TS *t)
{
    POINT pt;
    if (!t->host || !t->focus) return;
    caret_point(t, &pt);
    if (!t->caret) {
        HOST(t)->TxCreateCaret(t->host, 0, 1, t->lineh);
        t->caret = TRUE;
    }
    HOST(t)->TxSetCaretPos(t->host, pt.x, pt.y);
    HOST(t)->TxShowCaret(t->host, TRUE);
}

void redraw(TS *t)
{
    if (!t->host || t->frozen) return;            /* (ITextDocument::Freeze) */
    HOST(t)->TxInvalidateRect(t->host, 0, FALSE);
    HOST(t)->TxViewChange(t->host, TRUE);
}

void selection_changed(TS *t)
{
    scroll_to_caret(t);
    update_caret(t);
    redraw(t);
    if (t->mask & ENM_SELCHANGE) {
        SELCHANGE sc;
        memset(&sc, 0, sizeof(sc));
        sc.nmhdr.code = EN_SELCHANGE;
        sc.chrg.cpMin = selmin(t);
        sc.chrg.cpMax = selmax(t);
        sc.seltyp = sc.chrg.cpMin == sc.chrg.cpMax ? SEL_EMPTY : SEL_TEXT;
        notify(t, EN_SELCHANGE, &sc);
    }
}

void set_sel(TS *t, LONG anchor, LONG active)
{
    if (anchor < 0) anchor = 0;
    if (active < 0) active = 0;
    if (anchor > t->len) anchor = t->len;
    if (active > t->len) active = t->len;
    if (anchor == t->anchor && active == t->active) return;
    t->anchor = anchor;
    t->active = active;
    t->typing = FALSE;
    selection_changed(t);
}

/* ---- undo --------------------------------------------------------------- */
static void snap_free(SNAP *s) { release(s->text); s->text = 0; }

static BOOL snap_take(TS *t, SNAP *s)
{
    s->text = alloc((t->len + 1) * sizeof(WCHAR));
    if (!s->text) return FALSE;
    memcpy(s->text, t->text, t->len * sizeof(WCHAR));
    s->len = t->len;
    s->anchor = t->anchor;
    s->active = t->active;
    return TRUE;
}

static void push(SNAP *stack, int *n, int limit, SNAP *s)
{
    if (limit <= 0) { snap_free(s); return; }
    if (*n >= limit) {
        snap_free(&stack[0]);
        memmove(stack, stack + 1, (*n - 1) * sizeof(SNAP));
        (*n)--;
    }
    stack[(*n)++] = *s;
}

static void clear_stack(SNAP *stack, int *n)
{
    while (*n) snap_free(&stack[--(*n)]);
}

static void save_undo(TS *t)
{
    SNAP s;
    if (t->typing) return;                        /* (typing groups into one step) */
    if (t->undooff || t->undosuspend) return;     /* (ITextDocument::Undo(tomFalse or tomSuspend)) */
    if (!snap_take(t, &s)) return;
    push(t->undo, &t->nundo, t->undolimit, &s);
    clear_stack(t->redo, &t->nredo);
}

void clear_undo(TS *t)
{
    clear_stack(t->undo, &t->nundo);
    clear_stack(t->redo, &t->nredo);
}

static void restore(TS *t, SNAP *s)
{
    if (reserve(t, s->len)) {
        memcpy(t->text, s->text, s->len * sizeof(WCHAR));
        t->len = s->len;
    }
    t->anchor = s->anchor;
    t->active = s->active;
    snap_free(s);
    tom_clamp(t);
}

void changed(TS *t)
{
    t->dirty = TRUE;
    t->modified = TRUE;
    selection_changed(t);
    if (t->mask & ENM_CHANGE) {
        /* (a windowless host's EN_CHANGE carries what changed: XAML's text
         * boxes read it) */
        CHANGENOTIFY cn = { CN_TEXTCHANGED, 0 };
        notify(t, EN_CHANGE, &cn);
    }
}

BOOL undo_redo(TS *t, BOOL redo)
{
    SNAP cur, *from = redo ? t->redo : t->undo, *to = redo ? t->undo : t->redo;
    int *nfrom = redo ? &t->nredo : &t->nundo, *nto = redo ? &t->nundo : &t->nredo;
    if (!*nfrom || !snap_take(t, &cur)) return FALSE;
    push(to, nto, t->undolimit, &cur);
    restore(t, &from[--(*nfrom)]);
    t->typing = FALSE;
    changed(t);
    return TRUE;
}

/* ---- editing ------------------------------------------------------------ */
/* the text as stored: "\r\n" and "\n" become '\r'; one line keeps its first paragraph */
static LONG normalise(TS *t, const WCHAR *s, LONG n, WCHAR *out)
{
    LONG i, k = 0;
    for (i = 0; i < n; i++) {
        WCHAR c = s[i];
        if (c == L'\r' && i + 1 < n && s[i + 1] == L'\n') i++;
        if (c == L'\n') c = L'\r';
        if (c == L'\r' && !(t->bits & TXTBIT_MULTILINE)) break;
        if (out) out[k] = c;
        k++;
    }
    return k;
}

/* Replaces [a, b) with @s (n characters, or to its end when n < 0), as far
 * as the limit lets it: the one place the text changes, so the Text Object
 * Model's ranges and the selection follow it.  Returns the number inserted,
 * or -1 when nothing changed */
LONG splice(TS *t, LONG a, LONG b, const WCHAR *s, LONG n, BOOL undoable, BOOL typed)
{
    LONG k, room;
    WCHAR *norm;
    if (a < 0) a = 0;
    if (b > t->len) b = t->len;
    if (a > b) a = b;
    if (n < 0) { n = 0; while (s && s[n]) n++; }
    if (!(norm = alloc((n + 1) * sizeof(WCHAR)))) return -1;
    k = normalise(t, s, n, norm);
    /* (the limit is a DWORD: a host with no limit, XAML's text boxes,
     * answers TxGetMaxLength INFINITE) */
    room = t->maxlen > 0x7fffffff ? 0x7fffffff - (t->len - (b - a)) : (LONG)t->maxlen - (t->len - (b - a));
    if (room < 0) room = 0;
    if (k > room) { k = room; notify(t, EN_MAXTEXT, 0); }
    if (k == 0 && a == b) { release(norm); return -1; }
    if (!typed) t->typing = FALSE;
    if (undoable) save_undo(t);
    else if (!typed) { clear_stack(t->undo, &t->nundo); clear_stack(t->redo, &t->nredo); }
    if (!reserve(t, t->len - (b - a) + k)) { release(norm); return -1; }
    memmove(t->text + a + k, t->text + b, (t->len - b) * sizeof(WCHAR));
    memcpy(t->text + a, norm, k * sizeof(WCHAR));
    t->len += k - (b - a);
    t->typing = typed;
    release(norm);
    tom_edited(t, a, b, k);
    t->dirty = TRUE;
    t->targetx = -1;
    return k;
}

/* Replaces the selection with @s; the caret goes after it.  Returns the
 * number inserted */
LONG replace_sel(TS *t, const WCHAR *s, LONG n, BOOL undoable, BOOL typed)
{
    LONG a = selmin(t), k = splice(t, a, selmax(t), s, n, undoable, typed);
    if (k < 0) return 0;
    t->anchor = t->active = a + k;
    changed(t);
    t->targetx = -1;
    return k;
}

void set_text(TS *t, const WCHAR *s, LONG n)
{
    t->anchor = 0;
    t->active = t->len;
    t->typing = FALSE;
    clear_stack(t->undo, &t->nundo);
    clear_stack(t->redo, &t->nredo);
    replace_sel(t, s ? s : L"", s ? n : 0, FALSE, FALSE);
    t->anchor = t->active = 0;                     /* (the caret goes to the start, as Rich Edit's) */
    tom_clamp(t);
    t->scrollx = t->scrolly = 0;
    t->modified = FALSE;
    selection_changed(t);
}

BOOL is_word(WCHAR c) { return c != L' ' && c != L'\t' && c != L'\r'; }

static LONG word_left(TS *t, LONG cp)
{
    while (cp > 0 && !is_word(t->text[cp - 1])) cp--;
    while (cp > 0 && is_word(t->text[cp - 1])) cp--;
    return cp;
}

static LONG word_right(TS *t, LONG cp)
{
    while (cp < t->len && is_word(t->text[cp])) cp++;
    while (cp < t->len && !is_word(t->text[cp])) cp++;
    return cp;
}

/* ---- the clipboard ------------------------------------------------------ */
HWND host_window(TS *t)
{
    ITextHost2 *h2 = 0;
    HWND w = 0;
    static const IID iid_host2 = NOVA_IID_ITextHost2;
    if (t->host && HOST(t)->QueryInterface(t->host, &iid_host2, (void **)&h2) == S_OK && h2) {
        h2->lpVtbl->TxGetWindow(h2, &w);
        h2->lpVtbl->Release(h2);
    }
    return w;
}

BOOL copy_range(TS *t, LONG a, LONG b)
{
    LONG i, k = 0;
    HGLOBAL mem;
    WCHAR *p;
    if (a == b || (t->bits & TXTBIT_USEPASSWORD)) return FALSE;   /* (a password is never copied) */
    for (i = a; i < b; i++) k += t->text[i] == L'\r' ? 2 : 1;
    if (!(mem = GlobalAlloc(GMEM_MOVEABLE, (k + 1) * sizeof(WCHAR)))) return FALSE;
    p = GlobalLock(mem);
    for (i = a, k = 0; i < b; i++) {
        p[k++] = t->text[i];
        if (t->text[i] == L'\r') p[k++] = L'\n';
    }
    p[k] = 0;
    GlobalUnlock(mem);
    if (!OpenClipboard(host_window(t))) { GlobalFree(mem); return FALSE; }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
    CloseClipboard();
    return TRUE;
}

static BOOL copy_sel(TS *t) { return copy_range(t, selmin(t), selmax(t)); }

/* a cut, copy or paste the user asked for (keys), offered to the host
 * first: WinUIEdit's EN_CUTREQUESTED, EN_COPYREQUESTED, EN_PASTEREQUESTED,
 * whose data is a bool the host sets when it handled it (XAML's text
 * boxes raise their Cutting/CopyingToClipboard and Paste events, and an app
 * that handles one does it its own way); FALSE: go ahead */
BOOL host_handles(TS *t, DWORD code)
{
    unsigned char handled = 0;                    /* (C++'s bool) */
    notify(t, code, &handled);
    return handled != 0;
}

static void key_copy(TS *t) { if (!host_handles(t, EN_COPYREQUESTED_)) copy_sel(t); }
static void key_cut(TS *t)
{
    if ((t->bits & TXTBIT_READONLY) || host_handles(t, EN_CUTREQUESTED_)) return;
    if (copy_sel(t)) replace_sel(t, L"", 0, TRUE, FALSE);
}
static void paste(TS *t);
static void key_paste(TS *t) { if (!(t->bits & TXTBIT_READONLY) && !host_handles(t, EN_PASTEREQUESTED_)) paste(t); }

BOOL can_paste(TS *t)
{
    (void)t;
    return IsClipboardFormatAvailable(CF_UNICODETEXT);
}

/* the clipboard's text (to release), or NULL */
WCHAR *clipboard_text(TS *t)
{
    HGLOBAL mem;
    WCHAR *out = 0;
    if (!OpenClipboard(host_window(t))) return 0;
    if ((mem = GetClipboardData(CF_UNICODETEXT))) {
        const WCHAR *p = GlobalLock(mem);
        if (p) {
            SIZE_T n = 0;
            while (p[n]) n++;
            if ((out = alloc((n + 1) * sizeof(WCHAR)))) memcpy(out, p, (n + 1) * sizeof(WCHAR));
            GlobalUnlock(mem);
        }
    }
    CloseClipboard();
    return out;
}

static void paste(TS *t)
{
    WCHAR *p;
    if (t->bits & TXTBIT_READONLY) return;
    if ((p = clipboard_text(t))) { replace_sel(t, p, -1, TRUE, FALSE); release(p); }
}

/* ---- keys --------------------------------------------------------------- */
static BOOL down(int vk) { return GetKeyState(vk) < 0; }

static HRESULT on_char(TS *t, WCHAR c)
{
    if (c == 8) {                                 /* Backspace */
        if (t->bits & TXTBIT_READONLY) return S_OK;
        if (selmin(t) == selmax(t)) {
            if (!t->active) return S_OK;
            t->anchor = t->active - 1;
        }
        replace_sel(t, L"", 0, TRUE, FALSE);
        return S_OK;
    }
    if (c == L'\r' || c == L'\n') {
        if (!(t->bits & TXTBIT_MULTILINE)) return S_FALSE;   /* (the host's: a default button) */
        c = L'\r';
    } else if (c < 0x20 && c != L'\t') return S_FALSE;        /* Ctrl+letter: WM_KEYDOWN's */
    else if (c == 0x7f) return S_FALSE;
    if (t->bits & TXTBIT_READONLY) return S_OK;
    replace_sel(t, &c, 1, TRUE, TRUE);
    return S_OK;
}

static HRESULT on_key(TS *t, UINT vk)
{
    BOOL shift = down(VK_SHIFT), ctrl = down(VK_CONTROL);
    LONG cp = t->active, a;
    int l;
    ensure_layout(t);
    switch (vk) {
    case VK_LEFT:
        if (!shift && selmin(t) != selmax(t) && !ctrl) cp = selmin(t);
        else cp = ctrl ? word_left(t, cp) : (cp > 0 ? cp - 1 : 0);
        break;
    case VK_RIGHT:
        if (!shift && selmin(t) != selmax(t) && !ctrl) cp = selmax(t);
        else cp = ctrl ? word_right(t, cp) : (cp < t->len ? cp + 1 : t->len);
        break;
    case VK_HOME:
        cp = ctrl ? 0 : t->lines[line_of(t, cp)].start;
        break;
    case VK_END:
        l = line_of(t, cp);
        cp = ctrl ? t->len : t->lines[l].start + t->lines[l].len;
        if (!ctrl && l + 1 < t->nlines && cp == t->lines[l + 1].start && cp > t->lines[l].start) cp--;
        break;
    case VK_UP: case VK_DOWN:
        l = line_of(t, cp);
        if (t->targetx < 0) t->targetx = x_of(t, cp);
        if (vk == VK_UP && l > 0) cp = cp_at_x(t, l - 1, t->targetx);
        else if (vk == VK_DOWN && l + 1 < t->nlines) cp = cp_at_x(t, l + 1, t->targetx);
        else cp = vk == VK_UP ? 0 : t->len;
        a = t->targetx;
        set_sel(t, shift ? t->anchor : cp, cp);
        t->targetx = a;
        return S_OK;
    case VK_DELETE:
        if (t->bits & TXTBIT_READONLY) return S_OK;
        if (shift) { key_cut(t); return S_OK; }
        if (selmin(t) == selmax(t)) {
            if (t->active >= t->len) return S_OK;
            t->anchor = ctrl ? word_right(t, t->active) : t->active + 1;
        }
        replace_sel(t, L"", 0, TRUE, FALSE);
        return S_OK;
    case VK_INSERT:
        if (ctrl) key_copy(t);
        else if (shift) key_paste(t);
        return S_OK;
    default:
        if (!ctrl || down(VK_MENU)) return S_FALSE;
        switch (vk) {
        case 'A': set_sel(t, 0, t->len); return S_OK;
        case 'C': key_copy(t); return S_OK;
        case 'X': key_cut(t); return S_OK;
        case 'V': key_paste(t); return S_OK;
        case 'Z': if (!(t->bits & TXTBIT_READONLY)) undo_redo(t, FALSE); return S_OK;
        case 'Y': if (!(t->bits & TXTBIT_READONLY)) undo_redo(t, TRUE); return S_OK;
        }
        return S_FALSE;
    }
    t->targetx = -1;
    set_sel(t, shift ? t->anchor : cp, cp);
    return S_OK;
}

/* ---- text out ----------------------------------------------------------- */
/* the text with "\r\n" line ends (WM_GETTEXT, EM_GETTEXTEX GT_USECRLF) */
static LONG crlf_len(TS *t, LONG a, LONG b)
{
    LONG i, n = 0;
    for (i = a; i < b; i++) n += t->text[i] == L'\r' ? 2 : 1;
    return n;
}

static LONG copy_out(TS *t, LONG a, LONG b, BOOL crlf, WCHAR *out, LONG cap)
{
    LONG i, k = 0;
    if (cap <= 0) return 0;
    for (i = a; i < b && k < cap - 1; i++) {
        if (crlf && t->text[i] == L'\r') {
            if (k + 2 > cap - 1) break;
            out[k++] = L'\r';
            out[k++] = L'\n';
        } else out[k++] = t->text[i];
    }
    out[k] = 0;
    return k;
}

typedef struct { CHARRANGE chrg; LPWSTR text; } TEXTRANGEW_;

/* ---- TxSendMessage ------------------------------------------------------ */
static HRESULT send(TS *t, UINT msg, WPARAM wp, LPARAM lp, LRESULT *res)
{
    LRESULT r = 0;
    HRESULT hr = S_OK;
    LONG a, b;
    switch (msg) {
    case WM_CHAR:
        hr = on_char(t, (WCHAR)wp);
        break;
    case WM_KEYDOWN:
        hr = on_key(t, (UINT)wp);
        break;
    case WM_SETFOCUS:
        t->focus = TRUE;
        update_caret(t);
        redraw(t);
        break;
    case WM_KILLFOCUS:
        t->focus = FALSE;
        if (t->caret && t->host) HOST(t)->TxShowCaret(t->host, FALSE);
        t->caret = FALSE;
        t->dragging = FALSE;
        redraw(t);
        break;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
        LONG cp = hit(t, (short)LOWORD(lp), (short)HIWORD(lp));
        if (t->host && !t->focus) HOST(t)->TxSetFocus(t->host);
        if (msg == WM_LBUTTONDBLCLK) {
            LONG s = cp, e = cp;
            while (s > 0 && is_word(t->text[s - 1])) s--;
            while (e < t->len && is_word(t->text[e])) e++;
            set_sel(t, s, e);
        } else set_sel(t, (wp & MK_SHIFT) ? t->anchor : cp, cp);
        t->dragging = TRUE;
        t->targetx = -1;
        if (t->host) HOST(t)->TxSetCapture(t->host, TRUE);
        break;
    }
    case WM_MOUSEMOVE:
        if (t->dragging && (wp & MK_LBUTTON)) set_sel(t, t->anchor, hit(t, (short)LOWORD(lp), (short)HIWORD(lp)));
        break;
    case NOVA_EM_SETDPI:
        r = TRUE;
        break;
    case NOVA_EM_MOUSEINPUT: {                    /* XAML's text boxes: lp = NOVA_RE_MOUSEINPUT * */
        const NOVA_RE_MOUSEINPUT *in = (const NOVA_RE_MOUSEINPUT *)lp;
        if (!in) return E_INVALIDARG;
        return send(t, in->msg, in->wparam, MAKELPARAM((WORD)in->pt.x, (WORD)in->pt.y), res);
    }
    case WM_LBUTTONUP:
        if (t->dragging && t->host) HOST(t)->TxSetCapture(t->host, FALSE);
        t->dragging = FALSE;
        break;
    case WM_SETTEXT:
        set_text(t, (const WCHAR *)lp, -1);
        r = TRUE;
        break;
    case WM_GETTEXTLENGTH:
        r = crlf_len(t, 0, t->len);
        break;
    case WM_GETTEXT:
        r = copy_out(t, 0, t->len, TRUE, (WCHAR *)lp, (LONG)wp);
        break;
    case EM_GETTEXTLENGTHEX: {
        const GETTEXTLENGTHEX *g = (const GETTEXTLENGTHEX *)wp;
        r = (g && (g->flags & GT_USECRLF)) ? crlf_len(t, 0, t->len) : t->len;
        break;
    }
    case EM_GETTEXTEX: {
        const GETTEXTEX *g = (const GETTEXTEX *)wp;
        BOOL crlf = g && (g->flags & GT_USECRLF);
        a = (g && (g->flags & GT_SELECTION)) ? selmin(t) : 0;
        b = (g && (g->flags & GT_SELECTION)) ? selmax(t) : t->len;
        if (!g || !lp) break;
        if (g->codepage == 1200) r = copy_out(t, a, b, crlf, (WCHAR *)lp, (LONG)(g->cb / sizeof(WCHAR)));
        else {
            LONG n = (crlf ? crlf_len(t, a, b) : b - a) + 1;
            WCHAR *w = alloc(n * sizeof(WCHAR));
            if (!w) break;
            copy_out(t, a, b, crlf, w, n);
            r = WideCharToMultiByte(g->codepage, 0, w, -1, (char *)lp, (int)g->cb, g->lpDefaultChar, g->lpUsedDefChar);
            if (r > 0) r--;
            release(w);
        }
        break;
    }
    case EM_SETTEXTEX: {
        const SETTEXTEX *s = (const SETTEXTEX *)wp;
        WCHAR *w = 0;
        const WCHAR *src = (const WCHAR *)lp;
        if (s && s->codepage != 1200 && lp) {
            int n = MultiByteToWideChar(s->codepage, 0, (const char *)lp, -1, 0, 0);
            if (n > 0 && (w = alloc(n * sizeof(WCHAR)))) MultiByteToWideChar(s->codepage, 0, (const char *)lp, -1, w, n);
            src = w;
        }
        if (s && (s->flags & ST_SELECTION)) r = replace_sel(t, src ? src : L"", -1, (s->flags & ST_KEEPUNDO) != 0, FALSE) >= 0;
        else { set_text(t, src, -1); r = 1; }
        release(w);
        break;
    }
    case EM_REPLACESEL:
        if (!lp) break;
        replace_sel(t, (const WCHAR *)lp, -1, wp != 0, FALSE);
        break;
    case EM_GETSEL:
        if (wp) *(DWORD *)wp = selmin(t);
        if (lp) *(DWORD *)lp = selmax(t);
        r = MAKELONG(selmin(t) > 0xffff ? 0xffff : selmin(t), selmax(t) > 0xffff ? 0xffff : selmax(t));
        break;
    case EM_SETSEL:
        a = (LONG)wp;
        b = (LONG)lp;
        if (a == -1) b = a = t->active;           /* -1: no selection, the caret stays */
        else if (b == -1 || b > t->len) b = t->len;
        if (a > t->len) a = t->len;
        set_sel(t, a, b);
        r = selmax(t);
        break;
    case EM_EXGETSEL:
        if (lp) { ((CHARRANGE *)lp)->cpMin = selmin(t); ((CHARRANGE *)lp)->cpMax = selmax(t); }
        break;
    case EM_EXSETSEL:
        if (!lp) break;
        a = ((CHARRANGE *)lp)->cpMin;
        b = ((CHARRANGE *)lp)->cpMax;
        if (a < 0) b = a = t->active;
        else if (b < 0 || b > t->len) b = t->len;
        if (a > t->len) a = t->len;
        set_sel(t, a, b);
        r = selmax(t);
        break;
    case EM_SELECTIONTYPE:
        r = selmin(t) == selmax(t) ? SEL_EMPTY : SEL_TEXT;
        break;
    case EM_GETSELTEXT:
        if (lp) r = copy_out(t, selmin(t), selmax(t), FALSE, (WCHAR *)lp, selmax(t) - selmin(t) + 1);
        break;
    case EM_GETTEXTRANGE: {
        TEXTRANGEW_ *tr = (TEXTRANGEW_ *)lp;
        if (!tr || !tr->text) break;
        a = tr->chrg.cpMin;
        b = tr->chrg.cpMax < 0 || tr->chrg.cpMax > t->len ? t->len : tr->chrg.cpMax;
        if (a < 0) a = 0;
        if (a > b) a = b;
        r = copy_out(t, a, b, FALSE, tr->text, b - a + 1);
        break;
    }
    case EM_LIMITTEXT: case EM_EXLIMITTEXT:
        t->maxlen = lp || msg == EM_LIMITTEXT ? (DWORD)(msg == EM_LIMITTEXT ? wp : lp) : 65536;
        if (!t->maxlen) t->maxlen = 65536;
        break;
    case EM_GETLIMITTEXT:
        r = t->maxlen;
        break;
    case EM_SETREADONLY:
        if (wp) t->bits |= TXTBIT_READONLY; else t->bits &= ~TXTBIT_READONLY;
        r = TRUE;
        break;
    case EM_GETEVENTMASK:
        r = t->mask;
        break;
    case EM_SETEVENTMASK:
        r = t->mask;
        t->mask = (DWORD)lp;
        break;
    case EM_GETMODIFY:
        r = t->modified ? -1 : 0;
        break;
    case EM_SETMODIFY:
        t->modified = wp != 0;
        break;
    case EM_CANUNDO:
        r = t->nundo > 0;
        break;
    case EM_CANREDO:
        r = t->nredo > 0;
        break;
    case EM_UNDO: case WM_UNDO:
        r = !(t->bits & TXTBIT_READONLY) && undo_redo(t, FALSE);
        break;
    case EM_REDO:
        r = !(t->bits & TXTBIT_READONLY) && undo_redo(t, TRUE);
        break;
    case EM_EMPTYUNDOBUFFER:
        clear_stack(t->undo, &t->nundo);
        clear_stack(t->redo, &t->nredo);
        t->typing = FALSE;
        break;
    case EM_STOPGROUPTYPING:
        t->typing = FALSE;
        break;
    case EM_SETUNDOLIMIT:
        t->undolimit = wp > UNDO_MAX ? UNDO_MAX : (int)wp;
        while (t->nundo > t->undolimit) { snap_free(&t->undo[0]); memmove(t->undo, t->undo + 1, (--t->nundo) * sizeof(SNAP)); }
        r = t->undolimit;
        break;
    case WM_COPY:
        copy_sel(t);
        break;
    case WM_CUT:
        if (!(t->bits & TXTBIT_READONLY) && copy_sel(t)) replace_sel(t, L"", 0, TRUE, FALSE);
        break;
    case WM_PASTE:
        paste(t);
        break;
    case WM_CLEAR:
        if (!(t->bits & TXTBIT_READONLY)) replace_sel(t, L"", 0, TRUE, FALSE);
        break;
    case EM_CANPASTE:
        r = !(t->bits & TXTBIT_READONLY) && can_paste(t);
        break;
    case EM_GETLINECOUNT:
        ensure_layout(t);
        r = t->nlines;
        break;
    case EM_LINEFROMCHAR: case EM_EXLINEFROMCHAR:
        ensure_layout(t);
        a = (LONG)(msg == EM_EXLINEFROMCHAR ? lp : wp);
        r = line_of(t, a < 0 ? t->active : (a > t->len ? t->len : a));
        break;
    case EM_LINEINDEX:
        ensure_layout(t);
        a = (LONG)wp < 0 ? line_of(t, t->active) : (LONG)wp;
        r = a < t->nlines ? t->lines[a].start : -1;
        break;
    case EM_LINELENGTH:
        ensure_layout(t);
        a = (LONG)wp < 0 || (LONG)wp > t->len ? t->active : (LONG)wp;
        r = t->lines[line_of(t, a)].len;
        break;
    case EM_POSFROMCHAR: {                        /* Rich Edit 2.0+: wp = POINTL *, lp = cp */
        POINTL *pt = (POINTL *)wp;
        LONG cp = (LONG)lp < 0 ? 0 : ((LONG)lp > t->len ? t->len : (LONG)lp);
        RECT rc;
        if (!pt) break;
        ensure_layout(t);
        client_rect(t, &rc);
        pt->x = rc.left + x_of(t, cp) - t->scrollx;
        pt->y = rc.top + line_of(t, cp) * t->lineh - t->scrolly;
        break;
    }
    case EM_CHARFROMPOS:
        if (lp) r = hit(t, ((POINTL *)lp)->x, ((POINTL *)lp)->y);
        break;
    case EM_SETCHARFORMAT: {
        const CHARFORMATW *cf = (const CHARFORMATW *)lp;
        if (!cf) { r = 0; break; }
        if (cf->dwMask & CFM_FACE) memcpy(t->cf.cf.szFaceName, cf->szFaceName, sizeof(cf->szFaceName));
        if (cf->dwMask & CFM_SIZE) t->cf.cf.yHeight = cf->yHeight;
        if (cf->dwMask & CFM_COLOR) {
            t->cf.cf.crTextColor = cf->crTextColor;
            t->cf.cf.dwEffects = (t->cf.cf.dwEffects & ~CFE_AUTOCOLOR) | (cf->dwEffects & CFE_AUTOCOLOR);
        }
        if (cf->dwMask & CFM_BOLD) t->cf.cf.dwEffects = (t->cf.cf.dwEffects & ~CFE_BOLD) | (cf->dwEffects & CFE_BOLD);
        if (cf->dwMask & CFM_ITALIC) t->cf.cf.dwEffects = (t->cf.cf.dwEffects & ~CFE_ITALIC) | (cf->dwEffects & CFE_ITALIC);
        if (cf->dwMask & CFM_UNDERLINE) t->cf.cf.dwEffects = (t->cf.cf.dwEffects & ~CFE_UNDERLINE) | (cf->dwEffects & CFE_UNDERLINE);
        t->cf.cf.dwMask |= cf->dwMask;
        {   /* (the font, without reading the host's format again) */
            ITextHost *h = t->host;
            t->host = 0;
            refresh_format(t);
            t->host = h;
        }
        redraw(t);
        r = 1;
        break;
    }
    case EM_GETCHARFORMAT: {
        CHARFORMATW *cf = (CHARFORMATW *)lp;
        UINT n;
        if (!cf) break;
        n = cf->cbSize < sizeof(t->cf) ? cf->cbSize : sizeof(t->cf);
        memcpy((char *)cf + sizeof(UINT), (char *)&t->cf + sizeof(UINT), n - sizeof(UINT));
        cf->dwMask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_SIZE | CFM_COLOR | CFM_FACE;
        r = cf->dwMask;
        break;
    }
    case EM_GETPARAFORMAT: {
        PARAFORMAT *pf = (PARAFORMAT *)lp;
        if (pf && pf->cbSize >= sizeof(UINT) + sizeof(DWORD)) { pf->dwMask = 0; }
        break;
    }
    case EM_SETPARAFORMAT:
        r = TRUE;
        break;
    case EM_GETOPTIONS:
        r = t->options;
        break;
    case EM_SETOPTIONS:
        if (wp == 1) t->options = (DWORD)lp;             /* ECOOP_SET */
        else if (wp == 2) t->options |= (DWORD)lp;       /* ECOOP_OR */
        else if (wp == 3) t->options &= (DWORD)lp;       /* ECOOP_AND */
        else if (wp == 4) t->options ^= (DWORD)lp;       /* ECOOP_XOR */
        r = t->options;
        break;
    case EM_GETEDITSTYLE:
        r = t->editstyle;
        break;
    case EM_SETEDITSTYLE:
        t->editstyle = (t->editstyle & ~(DWORD)lp) | ((DWORD)wp & (DWORD)lp);
        r = t->editstyle;
        break;
    case EM_GETLANGOPTIONS:
        r = t->langopts;
        break;
    case EM_SETLANGOPTIONS:
        t->langopts = (DWORD)lp;
        r = 1;
        break;
    case EM_GETTEXTMODE:
        r = t->textmode;
        break;
    case EM_SETTEXTMODE:
        if (t->len) { r = 1; break; }             /* (only while empty, as Rich Edit) */
        t->textmode = (DWORD)wp;
        break;
    case EM_AUTOURLDETECT: case EM_SETOLECALLBACK: case EM_HIDESELECTION:
        r = msg == EM_SETOLECALLBACK;
        break;
    case EM_GETOLEINTERFACE:                      /* IRichEditOle, whose QueryInterface gives the TOM */
        if (!lp) break;
        *(void **)lp = &t->reole;
        t->outer->lpVtbl->AddRef(t->outer);
        r = TRUE;
        break;
    default:
        hr = S_FALSE;
        break;
    }
    if (res) *res = r;
    return hr;
}

/* ---- drawing ------------------------------------------------------------ */
static void draw(TS *t, HDC dc, const RECT *bounds)
{
    RECT rc = *bounds, cl;
    TXTBACKSTYLE back = TXTBACK_OPAQUE;
    COLORREF fg = (t->cf.cf.dwEffects & CFE_AUTOCOLOR) || !(t->cf.cf.dwMask & CFM_COLOR)
                  ? syscolor(t, COLOR_WINDOWTEXT) : t->cf.cf.crTextColor;
    COLORREF selbg = syscolor(t, COLOR_HIGHLIGHT), selfg = syscolor(t, COLOR_HIGHLIGHTTEXT);
    BOOL showsel = t->focus || !(t->bits & TXTBIT_HIDESELECTION);
    LONG smin = selmin(t), smax = selmax(t);
    HGDIOBJ old;
    HRGN clip;
    int i, savedc;
    if (t->host) HOST(t)->TxGetBackStyle(t->host, &back);
    if (t->mono) back = TXTBACK_TRANSPARENT;      /* (TxDrawD2D's passes: on black) */
    if (t->mono == 2) fg = selbg = selfg = RGB(255, 255, 255);
    savedc = SaveDC(dc);
    clip = CreateRectRgn(rc.left, rc.top, rc.right, rc.bottom);
    SelectClipRgn(dc, clip);
    DeleteObject(clip);
    if (back == TXTBACK_OPAQUE) {
        HBRUSH br = CreateSolidBrush(syscolor(t, COLOR_WINDOW));
        FillRect(dc, &rc, br);
        DeleteObject(br);
    }
    /* the text lays out in the drawn rectangle */
    (void)cl;
    if (t->dirty || !t->nlines
        || ((t->bits & TXTBIT_MULTILINE) && (t->bits & TXTBIT_WORDWRAP) && t->layoutw != rc.right - rc.left))
        layout(t, dc, rc.right - rc.left);
    old = SelectObject(dc, t->font);
    SetBkMode(dc, TRANSPARENT);
    for (i = 0; i < t->nlines; i++) {
        LINE *ln = &t->lines[i];
        LONG y = rc.top + i * t->lineh - t->scrolly, n = ln->len, k;
        WCHAR stackbuf[256], *s = stackbuf;
        int *dx = 0;
        if (y + t->lineh <= rc.top || y >= rc.bottom) continue;
        if (n > 256) s = alloc(n * sizeof(WCHAR));
        for (k = 0; k < n; k++) s[k] = shown(t, ln->start + k) == L'\t' ? L' ' : shown(t, ln->start + k);
        SetTextColor(dc, fg);
        if (n) ExtTextOutW(dc, rc.left - t->scrollx, y, 0, 0, s, (UINT)n, 0);
        /* the selected part again, over its highlight */
        if (showsel && smin < smax && smax > ln->start && smin <= ln->start + n) {
            LONG a = smin > ln->start ? smin - ln->start : 0, b = smax < ln->start + n ? smax - ln->start : n;
            BOOL eol = smax > ln->start + n && i + 1 < t->nlines;   /* (the paragraph mark) */
            if (n && (dx = alloc(n * sizeof(int)))) measure(t, dc, ln->start, n, dx, 0);
            {
                LONG x0 = rc.left - t->scrollx + (a ? dx[a - 1] : 0);
                LONG x1 = rc.left - t->scrollx + (b ? dx[b - 1] : 0) + (eol ? t->lineh / 3 : 0);
                RECT sr;
                HBRUSH br = CreateSolidBrush(selbg);
                SetRect(&sr, x0, y, x1, y + t->lineh);
                FillRect(dc, &sr, br);
                DeleteObject(br);
                SetTextColor(dc, selfg);
                if (b > a) ExtTextOutW(dc, x0, y, 0, 0, s + a, (UINT)(b - a), 0);
            }
            release(dx);
        }
        if (s != stackbuf) release(s);
    }
    SelectObject(dc, old);
    RestoreDC(dc, savedc);
}

/* ---- ITextServices2 ----------------------------------------------------- */
static HRESULT STDMETHODCALLTYPE ts_qi(ITextServices2 *me, REFIID iid, void **out)
{
    TS *t = FROM_IFACE(me);
    return t->outer->lpVtbl->QueryInterface(t->outer, iid, out);
}
static ULONG STDMETHODCALLTYPE ts_addref(ITextServices2 *me) { TS *t = FROM_IFACE(me); return t->outer->lpVtbl->AddRef(t->outer); }
static ULONG STDMETHODCALLTYPE ts_release(ITextServices2 *me) { TS *t = FROM_IFACE(me); return t->outer->lpVtbl->Release(t->outer); }

static HRESULT TXAPI ts_send(ITextServices2 *me, UINT msg, WPARAM wp, LPARAM lp, LRESULT *res)
{
    TS *t = FROM_IFACE(me);
    if (res) *res = 0;
    return send(t, msg, wp, lp, res);
}

static HRESULT TXAPI ts_draw(ITextServices2 *me, DWORD aspect, LONG lindex, void *info, DVTARGETDEVICE *td,
                             HDC hdc, HDC target, LPCRECTL bounds, LPCRECTL wbounds, LPRECT update,
                             TXCONTINUE cont, DWORD contparam, LONG view)
{
    TS *t = FROM_IFACE(me);
    RECT rc;
    BOOL host = FALSE;
    (void)aspect; (void)lindex; (void)info; (void)td; (void)target; (void)wbounds; (void)update;
    (void)cont; (void)contparam; (void)view;
    if (bounds) SetRect(&rc, bounds->left, bounds->top, bounds->right, bounds->bottom);
    else client_rect(t, &rc);
    if (!hdc) {
        if (!t->host || !(hdc = HOST(t)->TxGetDC(t->host))) return E_INVALIDARG;
        host = TRUE;
    }
    t->drawdc = hdc;
    draw(t, hdc, &rc);
    t->drawdc = 0;
    if (host) HOST(t)->TxReleaseDC(t->host, hdc);
    return S_OK;
}

static HRESULT TXAPI ts_scroll(ITextServices2 *me, LONG *min, LONG *max, LONG *pos, LONG *page, BOOL *enabled)
{
    (void)me;
    if (min) *min = 0;
    if (max) *max = 0;
    if (pos) *pos = 0;
    if (page) *page = 0;
    if (enabled) *enabled = FALSE;
    return S_OK;
}

static HRESULT TXAPI ts_setcursor(ITextServices2 *me, DWORD aspect, LONG lindex, void *info, DVTARGETDEVICE *td,
                                  HDC hdc, HDC target, LPCRECT client, INT x, INT y)
{
    TS *t = FROM_IFACE(me);
    (void)aspect; (void)lindex; (void)info; (void)td; (void)hdc; (void)target; (void)client; (void)x; (void)y;
    if (t->host) HOST(t)->TxSetCursor(t->host, LoadCursorW(0, IDC_IBEAM), TRUE);
    return S_OK;
}

static HRESULT TXAPI ts_hit(ITextServices2 *me, DWORD aspect, LONG lindex, void *info, DVTARGETDEVICE *td,
                            HDC hdc, HDC target, LPCRECT client, INT x, INT y, DWORD *res)
{
    TS *t = FROM_IFACE(me);
    RECT rc;
    (void)aspect; (void)lindex; (void)info; (void)td; (void)hdc; (void)target;
    if (!res) return E_INVALIDARG;
    if (client) rc = *client; else client_rect(t, &rc);
    *res = x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom ? HITRESULT_HIT : HITRESULT_OUTSIDE;
    return S_OK;
}

static HRESULT TXAPI ts_activate(ITextServices2 *me, LPCRECT client)
{
    TS *t = FROM_IFACE(me);
    t->inplace = TRUE;
    if (client) t->client = *client;
    t->dirty = TRUE;
    return S_OK;
}
static HRESULT TXAPI ts_deactivate(ITextServices2 *me) { FROM_IFACE(me)->inplace = FALSE; return S_OK; }
static HRESULT TXAPI ts_uiactivate(ITextServices2 *me) { (void)me; return S_OK; }
static HRESULT TXAPI ts_uideactivate(ITextServices2 *me) { (void)me; return S_OK; }

static HRESULT TXAPI ts_gettext(ITextServices2 *me, BSTR *out)
{
    TS *t = FROM_IFACE(me);
    if (!out) return E_INVALIDARG;
    *out = t->len ? SysAllocStringLen(t->text, (UINT)t->len) : 0;
    return t->len && !*out ? E_OUTOFMEMORY : S_OK;
}

static HRESULT TXAPI ts_settext(ITextServices2 *me, LPCWSTR s)
{
    set_text(FROM_IFACE(me), s, -1);
    return S_OK;
}

static HRESULT TXAPI ts_targetx(ITextServices2 *me, LONG *x)
{
    TS *t = FROM_IFACE(me);
    if (!x) return E_INVALIDARG;
    ensure_layout(t);
    *x = x_of(t, t->active);
    return S_OK;
}

static HRESULT TXAPI ts_baseline(ITextServices2 *me, LONG *pos)
{
    TS *t = FROM_IFACE(me);
    if (!pos) return E_INVALIDARG;
    ensure_layout(t);
    *pos = t->ascent;
    return S_OK;
}

static HRESULT natural(TS *t, HDC hdc, DWORD mode, LONG *width, LONG *height, LONG *ascent)
{
    LONG w = width && *width > 0 ? *width : 0, maxw = 0;
    int i;
    BOOL host = FALSE;
    (void)mode;
    if (!width || !height) return E_INVALIDARG;
    if (!hdc) hdc = get_dc(t, &host);
    t->drawdc = hdc;
    layout(t, hdc, w);
    t->drawdc = 0;
    put_dc(t, hdc, host);
    for (i = 0; i < t->nlines; i++) if (t->lines[i].width > maxw) maxw = t->lines[i].width;
    *width = maxw + 1;                            /* (room for the caret) */
    *height = t->nlines * t->lineh;
    if (ascent) *ascent = t->ascent;
    t->dirty = TRUE;                              /* (the next draw lays out for its own width) */
    return S_OK;
}

static HRESULT TXAPI ts_natural(ITextServices2 *me, DWORD aspect, HDC hdc, HDC target, DVTARGETDEVICE *td,
                                DWORD mode, const SIZEL *extent, LONG *width, LONG *height)
{
    (void)aspect; (void)target; (void)td; (void)extent;
    return natural(FROM_IFACE(me), hdc, mode, width, height, 0);
}

static HRESULT TXAPI ts_natural2(ITextServices2 *me, DWORD aspect, HDC hdc, HDC target, DVTARGETDEVICE *td,
                                 DWORD mode, const SIZEL *extent, LONG *width, LONG *height, LONG *ascent)
{
    (void)aspect; (void)target; (void)td; (void)extent;
    return natural(FROM_IFACE(me), hdc, mode, width, height, ascent);
}

static HRESULT TXAPI ts_droptarget(ITextServices2 *me, IDropTarget **out)
{
    (void)me;
    if (out) *out = 0;
    return E_NOTIMPL;
}

static HRESULT TXAPI ts_bits(ITextServices2 *me, DWORD mask, DWORD bits)
{
    TS *t = FROM_IFACE(me);
    t->bits = (t->bits & ~mask) | (bits & mask);
    if (mask & TXTBIT_CHARFORMATCHANGE) refresh_format(t);
    if (mask & (TXTBIT_MAXLENGTHCHANGE | TXTBIT_USEPASSWORD)) {
        DWORD max = 0;
        WCHAR ch = 0;
        if (t->host && HOST(t)->TxGetMaxLength(t->host, &max) == S_OK && max) t->maxlen = max;
        if (t->host && (t->bits & TXTBIT_USEPASSWORD) && HOST(t)->TxGetPasswordChar(t->host, &ch) == S_OK && ch) t->pwch = ch;
    }
    if (!(t->bits & TXTBIT_MULTILINE) && (mask & TXTBIT_MULTILINE)) {   /* (one line keeps its first paragraph) */
        LONG i = 0;
        while (i < t->len && t->text[i] != L'\r') i++;
        t->len = i;
        if (t->anchor > i) t->anchor = i;
        if (t->active > i) t->active = i;
        tom_clamp(t);
    }
    t->dirty = TRUE;
    redraw(t);
    return S_OK;
}

static HRESULT TXAPI ts_cached(ITextServices2 *me, DWORD *width, DWORD *height)
{
    TS *t = FROM_IFACE(me);
    LONG maxw = 0;
    int i;
    ensure_layout(t);
    for (i = 0; i < t->nlines; i++) if (t->lines[i].width > maxw) maxw = t->lines[i].width;
    if (width) *width = (DWORD)maxw;
    if (height) *height = (DWORD)(t->nlines * t->lineh);
    return S_OK;
}

/* Direct2D drawing: the text is drawn as TxDraw draws it, into a 32-bit
 * bitmap, which goes onto the render target with DrawBitmap.  When the host
 * wants the background left alone (TXTBACK_TRANSPARENT, as XAML's text
 * boxes do), the text is drawn twice on black: in its colours, which is
 * the colour already multiplied by the coverage, and all in white, whose
 * brightness is the coverage, so the bitmap is premultiplied BGRA */
typedef struct { UINT32 width, height; } D2DSIZEU_;
typedef struct { float left, top, right, bottom; } D2DRECTF_;
typedef struct { UINT32 format, alpha; float dpix, dpiy; } D2DBMPPROPS_;
typedef HRESULT (STDMETHODCALLTYPE *D2DCREATEBITMAP_)(void *, D2DSIZEU_, const void *, UINT32, const D2DBMPPROPS_ *, IUnknown **);
typedef void (STDMETHODCALLTYPE *D2DDRAWBITMAP_)(void *, IUnknown *, const D2DRECTF_ *, float, UINT32, const D2DRECTF_ *);

static HBITMAP dib(HDC dc, LONG w, LONG h, DWORD **bits)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;                   /* (top-down) */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)bits, 0, 0);
}

static void draw_pass(TS *t, HDC dc, HBITMAP bmp, DWORD *bits, LONG w, LONG h, int pass)
{
    HGDIOBJ old = SelectObject(dc, bmp);
    RECT rc;
    SetRect(&rc, 0, 0, w, h);
    memset(bits, 0, (SIZE_T)w * h * 4);
    t->mono = pass;
    t->drawdc = dc;
    draw(t, dc, &rc);
    t->drawdc = 0;
    t->mono = 0;
    GdiFlush();
    SelectObject(dc, old);
}

static HRESULT TXAPI ts_drawd2d(ITextServices2 *me, ID2D1RenderTarget_ *rt, LPCRECTL bounds, LPRECT update, LONG view)
{
    TS *t = FROM_IFACE(me);
    void **vt;
    RECT rc;
    LONG w, h, i;
    HDC dc;
    HBITMAP colour = 0, cover = 0;
    DWORD *cbits = 0, *abits = 0;
    TXTBACKSTYLE back = TXTBACK_OPAQUE;
    IUnknown *bitmap = 0;
    D2DSIZEU_ size;
    D2DBMPPROPS_ props = {87, 1, 96.0f, 96.0f};   /* DXGI_FORMAT_B8G8R8A8_UNORM, premultiplied */
    D2DRECTF_ dst;
    HRESULT hr;
    (void)update; (void)view;
    if (!rt) return E_INVALIDARG;
    if (bounds) SetRect(&rc, bounds->left, bounds->top, bounds->right, bounds->bottom);
    else client_rect(t, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return S_OK;
    if (!t->memdc) t->memdc = CreateCompatibleDC(0);
    dc = t->memdc;
    if (t->host) HOST(t)->TxGetBackStyle(t->host, &back);
    if (t->face) {
        /* the host's DirectWrite face: glyph runs (dwtext.c), laid out for
         * the drawn width first, as draw() does */
        if (t->dirty || !t->nlines
            || ((t->bits & TXTBIT_MULTILINE) && (t->bits & TXTBIT_WORDWRAP) && t->layoutw != w))
            layout(t, dc, w);
        return dw_draw(t, rt, &rc, back == TXTBACK_OPAQUE);
    }
    if (!(colour = dib(dc, w, h, &cbits))) return E_OUTOFMEMORY;
    if (back == TXTBACK_OPAQUE) {
        draw_pass(t, dc, colour, cbits, w, h, 0);
        for (i = 0; i < w * h; i++) cbits[i] |= 0xff000000;
    } else {
        if (!(cover = dib(dc, w, h, &abits))) { DeleteObject(colour); return E_OUTOFMEMORY; }
        draw_pass(t, dc, colour, cbits, w, h, 1);
        draw_pass(t, dc, cover, abits, w, h, 2);
        for (i = 0; i < w * h; i++) {
            DWORD a = abits[i], c = cbits[i], r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
            DWORD k = (a >> 16) & 0xff;
            if (((a >> 8) & 0xff) > k) k = (a >> 8) & 0xff;
            if ((a & 0xff) > k) k = a & 0xff;
            if (r > k) r = k;
            if (g > k) g = k;
            if (b > k) b = k;
            cbits[i] = (k << 24) | (r << 16) | (g << 8) | b;
        }
    }
    vt = *(void ***)rt;
    size.width = (UINT32)w;
    size.height = (UINT32)h;
    hr = ((D2DCREATEBITMAP_)vt[NOVA_D2D_CREATEBITMAP])(rt, size, cbits, (UINT32)w * 4, &props, &bitmap);
    if (SUCCEEDED(hr) && bitmap) {
        dst.left = (float)rc.left;
        dst.top = (float)rc.top;
        dst.right = (float)rc.right;
        dst.bottom = (float)rc.bottom;
        ((D2DDRAWBITMAP_)vt[NOVA_D2D_DRAWBITMAP])(rt, bitmap, &dst, 1.0f, 0, 0);   /* (nearest neighbour) */
        bitmap->lpVtbl->Release(bitmap);
    }
    DeleteObject(colour);
    if (cover) DeleteObject(cover);
    return hr;
}

static const ITextServices2Vtbl ts_vtbl = {
    ts_qi, ts_addref, ts_release,
    ts_send, ts_draw, ts_scroll, ts_scroll, ts_setcursor, ts_hit,
    ts_activate, ts_deactivate, ts_uiactivate, ts_uideactivate,
    ts_gettext, ts_settext, ts_targetx, ts_baseline, ts_natural, ts_droptarget,
    ts_bits, ts_cached, ts_natural2, ts_drawd2d,
};

/* ---- the object's own IUnknown ------------------------------------------ */
static HRESULT STDMETHODCALLTYPE in_qi(IUnknown *me, REFIID iid, void **out)
{
    TS *t = FROM_INNER(me);
    static const IID iid_ts = NOVA_IID_ITextServices, iid_ts2 = NOVA_IID_ITextServices2;
    if (!out) return E_POINTER;
    *out = 0;
    if (IsEqualGUID(iid, &iid_unknown)) *out = &t->inner;
    else if (IsEqualGUID(iid, &iid_ts) || IsEqualGUID(iid, &iid_ts2)) *out = &t->iface;
    else return tom_qi(t, iid, out);              /* (ITextDocument2 and the rest of the TOM) */
    ((IUnknown *)*out)->lpVtbl->AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE in_addref(IUnknown *me) { return (ULONG)InterlockedIncrement(&FROM_INNER(me)->ref); }

static ULONG STDMETHODCALLTYPE in_release(IUnknown *me)
{
    TS *t = FROM_INNER(me);
    LONG r = InterlockedDecrement(&t->ref);
    if (!r) {
        tom_free(t);
        dw_free(t);
        clear_stack(t->undo, &t->nundo);
        clear_stack(t->redo, &t->nredo);
        if (t->font) DeleteObject(t->font);
        if (t->memdc) DeleteDC(t->memdc);
        release(t->lines);
        release(t->text);
        release(t);
    }
    return (ULONG)r;
}

static const IUnknownVtbl in_vtbl = { in_qi, in_addref, in_release };

/* CreateTextServices: a text services object over @host, aggregated by
 * @outer when there is one.  The host is kept without AddRef, as on Windows
 * (XAML relies on that), until ShutdownTextServices or the last Release */
__declspec(dllexport) HRESULT WINAPI CreateTextServices(IUnknown *outer, ITextHost *host, IUnknown **out)
{
    TS *t;
    if (!out) return E_INVALIDARG;
    *out = 0;
    if (!(t = alloc(sizeof(TS)))) return E_OUTOFMEMORY;
    t->inner.lpVtbl = (IUnknownVtbl *)&in_vtbl;
    t->iface.lpVtbl = &ts_vtbl;
    t->ref = 1;
    t->outer = outer ? outer : &t->inner;
    t->host = host;
    t->maxlen = DEFAULT_MAX;
    t->pwch = L'*';
    t->undolimit = UNDO_MAX;
    t->targetx = -1;
    t->textmode = TM_RICHTEXT;
    reserve(t, 0);
    tom_init(t);
    refresh_bits(t);
    refresh_format(t);
    *out = &t->inner;
    return S_OK;
}

/* ShutdownTextServices: the host is going away; no more calls to it */
__declspec(dllexport) HRESULT WINAPI ShutdownTextServices(IUnknown *services)
{
    IUnknown *in = 0;
    static const IID iid_ts = NOVA_IID_ITextServices;
    ITextServices2 *ts = 0;
    if (!services) return E_INVALIDARG;
    if (services->lpVtbl->QueryInterface(services, &iid_ts, (void **)&ts) != S_OK || !ts) return E_INVALIDARG;
    if (ts->lpVtbl != &ts_vtbl) { ts->lpVtbl->Release(ts); return E_INVALIDARG; }
    FROM_IFACE(ts)->host = 0;
    FROM_IFACE(ts)->fontinfo = 0;                 /* (the host's) */
    ts->lpVtbl->Release(ts);
    (void)in;
    return S_OK;
}
