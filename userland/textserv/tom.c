/*
 * tom.c — the Text Object Model over the windowless Rich Edit's text
 * (textserv.c): ITextDocument2, which QueryInterface on the text services
 * gives (as XAML's text boxes get it), IRichEditOle (EM_GETOLEINTERFACE),
 * whose QueryInterface gives the same document, and the document's ranges
 * (ITextRange2) and selection (ITextSelection2).
 *
 * A range is a pair of character positions in the story.  The story is the
 * text and, as Rich Edit's, a final paragraph mark after it, so its length
 * is one more than the text's; ranges may take in that mark, the selection
 * may not, and no edit removes it.  Live ranges follow edits (whoever makes
 * them: typing, messages, other ranges), and a range outliving its text
 * services answers CO_E_RELEASED.  The selection's object reads and moves
 * the text services' own selection, so changing it notifies the host
 * (EN_SELCHANGE) and moves the caret as the keys do.
 *
 * What is here: text in and out (GetText/GetText2 with tomAllowFinalEOP
 * and tomUseCRLF, SetText, SetChar, TypeText), positions (Get/SetStart,
 * End, SetRange, Collapse, Select, InRange, IsEqual), the units character,
 * word, sentence (as paragraph), line, paragraph and story (Move, MoveStart,
 * MoveEnd, Expand, StartOf, EndOf, GetIndex, SetIndex, Delete, the
 * selection's MoveLeft/Right/Up/Down, HomeKey, EndKey), points (GetPoint,
 * SetPoint, GetRect, RangeFromPoint), FindText, ChangeCase, the clipboard
 * (Cut, Copy, Paste, CanPaste), undo and redo (with tomSuspend/tomResume
 * and tomFalse/tomTrue), Freeze/Unfreeze, the text limit, the client rect
 * and the input context.  The character and paragraph format objects
 * (ITextFont2, ITextPara2) are tomfmt.c's, with what XAML's TextBoxBase
 * asks of them; tables, math, embedded objects and IDispatch's late
 * binding answer E_NOTIMPL.  In a plain-text control the final paragraph
 * mark is never text (GetText2 with tomAllowFinalEOP leaves it out).  Written from the Text Object Model
 * documentation.
 */
#include "tsint.h"

#define CO_E_RELEASED_ ((HRESULT)0x800401ffL)
#define TA_RIGHT_    2
#define TA_CENTER_   6
#define TA_BOTTOM_   8
#define TA_BASELINE_ 24

static const IID iid_unknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID iid_dispatch = NOVA_IID_IDispatch;
static const IID iid_doc = NOVA_IID_ITextDocument, iid_doc2 = NOVA_IID_ITextDocument2;
static const IID iid_range = NOVA_IID_ITextRange, iid_range2 = NOVA_IID_ITextRange2;
static const IID iid_sel = NOVA_IID_ITextSelection, iid_sel2 = NOVA_IID_ITextSelection2;
static const IID iid_reole = NOVA_IID_IRichEditOle;

typedef struct RANGE {
    ITextRange2 iface;
    LONG ref;
    TS *t;                                        /* NULL once the text services are gone */
    LONG start, end;                              /* (the selection's: the text services') */
    BOOL sel;
    LONG gravity;
    struct RANGE *next;
} RANGE;

#define R(p) ((RANGE *)(p))
#define DOC(p) ((TS *)((char *)(p) - __builtin_offsetof(TS, doc)))
#define REOLE(p) ((TS *)((char *)(p) - __builtin_offsetof(TS, reole)))
#define LIVE(r) do { if (!(r)->t) return CO_E_RELEASED_; } while (0)
#define READONLY(t) (((t)->bits & TXTBIT_READONLY) != 0)

static const ITextRange2Vtbl range_vtbl;

/* ---- the story ---------------------------------------------------------- */
static LONG story_len(TS *t) { return t->len + 1; }   /* (and the final paragraph mark) */

static WCHAR char_at(TS *t, LONG cp)
{
    if (cp >= 0 && cp < t->len) return t->text[cp];
    return cp == t->len ? L'\r' : 0;
}

static LONG clamp(LONG cp, LONG lim) { return cp < 0 ? 0 : cp > lim ? lim : cp; }

static BOOL unit_ok(LONG unit)
{
    return unit == tomCharacter || unit == tomWord || unit == tomSentence || unit == tomParagraph
        || unit == tomLine || unit == tomStory;
}

/* Is @p the start of a @unit (or the story's end)?  A word is its letters
 * and the spaces after them; a paragraph mark is a word of its own */
static BOOL boundary(TS *t, LONG unit, LONG p)
{
    WCHAR a, b;
    int l;
    if (p <= 0 || p >= story_len(t)) return TRUE;
    switch (unit) {
    case tomCharacter: return TRUE;
    case tomWord:
        a = char_at(t, p - 1);
        b = char_at(t, p);
        if (a == L'\r' || b == L'\r') return TRUE;
        return !is_word(a) && is_word(b);
    case tomSentence: case tomParagraph:
        return char_at(t, p - 1) == L'\r';
    case tomLine:
        if (p > t->len) return TRUE;
        ensure_layout(t);
        l = line_of(t, p);
        return t->lines[l].start == p;
    }
    return FALSE;                                 /* (tomStory: its two ends only) */
}

static LONG next_b(TS *t, LONG unit, LONG p)
{
    LONG n = story_len(t);
    if (p >= n) return n;
    do p++; while (p < n && !boundary(t, unit, p));
    return p;
}

static LONG prev_b(TS *t, LONG unit, LONG p)
{
    if (p <= 0) return 0;
    do p--; while (p > 0 && !boundary(t, unit, p));
    return p;
}

static LONG unit_start(TS *t, LONG unit, LONG p) { return boundary(t, unit, p) ? p : prev_b(t, unit, p); }

/* @cp moved @count units (boundaries) within [0, lim]; *moved: how many */
static LONG move_by(TS *t, LONG unit, LONG cp, LONG count, LONG lim, LONG *moved)
{
    LONG m = 0;
    while (count > 0 && cp < lim) { cp = next_b(t, unit, cp); if (cp > lim) cp = lim; count--; m++; }
    while (count < 0 && cp > 0) { cp = prev_b(t, unit, cp); count++; m--; }
    if (moved) *moved = m;
    return cp;
}

/* ---- ranges ------------------------------------------------------------- */
static LONG lim_of(RANGE *r) { return r->sel ? r->t->len : story_len(r->t); }

static void get(RANGE *r, LONG *s, LONG *e)
{
    if (r->sel) { *s = selmin(r->t); *e = selmax(r->t); }
    else { *s = r->start; *e = r->end; }
}

/* the ends with the active one (the selection's caret; a range's end) */
static void get_ends(RANGE *r, LONG *anchor, LONG *active)
{
    if (r->sel) { *anchor = r->t->anchor; *active = r->t->active; }
    else { *anchor = r->start; *active = r->end; }
}

static void put_ends(RANGE *r, LONG anchor, LONG active)
{
    LONG lim = lim_of(r);
    anchor = clamp(anchor, lim);
    active = clamp(active, lim);
    if (r->sel) { set_sel(r->t, anchor, active); return; }
    r->start = anchor < active ? anchor : active;
    r->end = anchor < active ? active : anchor;
}

static void put(RANGE *r, LONG s, LONG e)
{
    if (s > e) { LONG x = s; s = e; e = x; }
    if (r->sel && r->t->active < r->t->anchor && s != e) put_ends(r, e, s);   /* (keeps the caret's end) */
    else put_ends(r, s, e);
}

static RANGE *new_range(TS *t, LONG s, LONG e, BOOL sel)
{
    RANGE *r = alloc(sizeof(RANGE));
    if (!r) return 0;
    r->iface.lpVtbl = &range_vtbl;
    r->ref = 1;
    r->t = t;
    r->sel = sel;
    if (!sel) {
        r->next = t->ranges;
        t->ranges = r;
        put_ends(r, s, e);
    }
    return r;
}

static RANGE *as_range(void *p)
{
    RANGE *r = p;
    return r && r->iface.lpVtbl == &range_vtbl ? r : 0;
}

/* [a, b) of the text replaced by @s: the range then holds the new text.
 * The final paragraph mark stays */
static HRESULT edit(RANGE *r, LONG a, LONG b, const WCHAR *s, LONG n)
{
    TS *t = r->t;
    LONG k;
    if (READONLY(t)) return E_ACCESSDENIED;
    if (b > t->len) b = t->len;
    if (a > b) a = b;
    k = splice(t, a, b, s, n, TRUE, FALSE);
    if (k < 0) {                                  /* (nothing changed: no EN_CHANGE) */
        if (r->sel) set_sel(t, a, a); else r->start = r->end = a;
        return S_OK;
    }
    if (r->sel) { t->anchor = a; t->active = a + k; }
    else { r->start = a; r->end = a + k; }
    changed(t);
    return S_OK;
}

static BSTR text_of(TS *t, LONG a, LONG b, LONG flags)
{
    LONG i, n = 0, k = 0;
    /* (a plain-text control's final paragraph mark is never text: XAML's
     * TextBox reads its Text with tomAllowFinalEOP, a RichEditBox's text
     * ends in it) */
    BOOL final = b > t->len && (flags & tomAllowFinalEOP) && (t->bits & TXTBIT_RICHTEXT);
    BSTR out;
    if (b > t->len) b = t->len;
    if (a > b) a = b;
    for (i = a; i < b; i++) n += (t->text[i] == L'\r' && (flags & tomUseCRLF)) ? 2 : 1;
    if (final) n += (flags & tomUseCRLF) ? 2 : 1;
    if (!(out = SysAllocStringLen(0, (UINT)n))) return 0;
    for (i = a; i < b; i++) {
        out[k++] = t->text[i];
        if (t->text[i] == L'\r' && (flags & tomUseCRLF)) out[k++] = L'\n';
    }
    if (final) {
        out[k++] = L'\r';
        if (flags & tomUseCRLF) out[k++] = L'\n';
    }
    out[k] = 0;
    return out;
}

/* client point of @cp; @type: TA_* alignment, tomClientCoord, tomAllowOffClient.
 * S_FALSE when off the client area and that is not allowed */
static HRESULT point_of(TS *t, LONG cp, LONG type, LONG *x, LONG *y)
{
    RECT rc;
    POINT pt;
    LONG top, h = type & TA_CENTER_;
    int l;
    if (cp > t->len) cp = t->len;
    ensure_layout(t);
    client_rect(t, &rc);
    l = line_of(t, cp);
    pt.x = rc.left + x_of(t, cp) - t->scrollx;
    if ((h == TA_RIGHT_ || h == TA_CENTER_) && cp < t->len && line_of(t, cp + 1) == l) {
        LONG x2 = rc.left + x_of(t, cp + 1) - t->scrollx;
        pt.x = h == TA_RIGHT_ ? x2 : (pt.x + x2) / 2;
    }
    top = rc.top + l * t->lineh - t->scrolly;
    switch (type & TA_BASELINE_) {
    case TA_BOTTOM_: pt.y = top + t->lineh; break;
    case TA_BASELINE_: pt.y = top + t->ascent; break;
    default: pt.y = top; break;
    }
    {
        BOOL off = pt.x < rc.left || pt.x > rc.right || pt.y < rc.top || pt.y > rc.bottom;
        if (!(type & tomClientCoord) && t->host) HOST(t)->TxClientToScreen(t->host, &pt);
        if (x) *x = pt.x;
        if (y) *y = pt.y;
        return off && !(type & tomAllowOffClient) ? S_FALSE : S_OK;
    }
}

static LONG cp_from_point(TS *t, LONG x, LONG y, LONG type)
{
    POINT pt;
    pt.x = x;
    pt.y = y;
    if (!(type & tomClientCoord) && t->host) HOST(t)->TxScreenToClient(t->host, &pt);
    return hit(t, pt.x, pt.y);
}

static WCHAR upper(WCHAR c) { return (WCHAR)(ULONG_PTR)CharUpperW((LPWSTR)(ULONG_PTR)c); }
static WCHAR lower(WCHAR c) { return (WCHAR)(ULONG_PTR)CharLowerW((LPWSTR)(ULONG_PTR)c); }

static BOOL match_at(TS *t, LONG p, const WCHAR *s, LONG n, LONG flags)
{
    LONG i;
    if (p < 0 || p + n > t->len) return FALSE;
    for (i = 0; i < n; i++) {
        WCHAR a = t->text[p + i], b = s[i] == L'\n' ? L'\r' : s[i];
        if (a != b && ((flags & tomMatchCase) || upper(a) != upper(b))) return FALSE;
    }
    if (flags & tomMatchWord) {
        if (p > 0 && is_word(t->text[p - 1]) && is_word(t->text[p])) return FALSE;
        if (p + n < t->len && is_word(t->text[p + n]) && is_word(t->text[p + n - 1])) return FALSE;
    }
    return TRUE;
}

/* ---- the edits follow ---------------------------------------------------- */
/* where @p goes when [a, b) becomes k characters.  Text inserted at a
 * range's start goes before it (@push), at its end after it, and an
 * insertion point stays before what is inserted there */
static LONG follow(LONG p, LONG a, LONG b, LONG k, BOOL push)
{
    if (a == b) return p > a || (p == a && push) ? p + k : p;
    if (p >= b) return p + k - (b - a);
    if (p > a) return a;                          /* (inside what was replaced) */
    return p;
}

void tom_edited(TS *t, LONG a, LONG b, LONG k)
{
    RANGE *r;
    for (r = t->ranges; r; r = r->next) {
        BOOL push = r->start < r->end;
        r->start = follow(r->start, a, b, k, push);
        r->end = follow(r->end, a, b, k, FALSE);
    }
    t->anchor = follow(t->anchor, a, b, k, FALSE);
    t->active = follow(t->active, a, b, k, FALSE);
}

void tom_clamp(TS *t)
{
    RANGE *r;
    for (r = t->ranges; r; r = r->next) {
        r->start = clamp(r->start, story_len(t));
        r->end = clamp(r->end, story_len(t));
    }
}

/* ---- ITextRange2 (and ITextSelection2) ---------------------------------- */
static HRESULT STDMETHODCALLTYPE r_qi(ITextRange2 *me, REFIID iid, void **out)
{
    RANGE *r = R(me);
    if (!out) return E_POINTER;
    *out = 0;
    if (IsEqualGUID(iid, &iid_unknown) || IsEqualGUID(iid, &iid_dispatch) || IsEqualGUID(iid, &iid_range)
        || IsEqualGUID(iid, &iid_range2) || (r->sel && (IsEqualGUID(iid, &iid_sel) || IsEqualGUID(iid, &iid_sel2)))) {
        *out = me;
        InterlockedIncrement(&r->ref);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE r_addref(ITextRange2 *me) { return (ULONG)InterlockedIncrement(&R(me)->ref); }

static ULONG STDMETHODCALLTYPE r_release(ITextRange2 *me)
{
    RANGE *r = R(me);
    LONG n = InterlockedDecrement(&r->ref);
    if (!n) {
        if (r->t && !r->sel) {
            RANGE **p = &r->t->ranges;
            while (*p && *p != r) p = &(*p)->next;
            if (*p) *p = r->next;
        }
        release(r);
    }
    return (ULONG)n;
}

/* IDispatch: no type information (late binding is not offered) */
static HRESULT STDMETHODCALLTYPE r_tcount(ITextRange2 *me, UINT *n) { (void)me; if (n) *n = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE r_tinfo(ITextRange2 *me, UINT i, LCID l, void **o) { (void)me; (void)i; (void)l; if (o) *o = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE r_ids(ITextRange2 *me, REFIID i, LPOLESTR *n, UINT c, LCID l, LONG *d)
{ (void)me; (void)i; (void)n; (void)c; (void)l; (void)d; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE r_invoke(ITextRange2 *me, LONG id, REFIID i, LCID l, WORD f, void *p, VARIANT *v, void *e, UINT *a)
{ (void)me; (void)id; (void)i; (void)l; (void)f; (void)p; (void)v; (void)e; (void)a; return E_NOTIMPL; }

static HRESULT STDMETHODCALLTYPE r_gettext2(ITextRange2 *me, LONG flags, BSTR *out)
{
    RANGE *r = R(me);
    LONG s, e;
    if (!out) return E_INVALIDARG;
    *out = 0;
    LIVE(r);
    get(r, &s, &e);
    *out = text_of(r->t, s, e, flags);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE r_gettext(ITextRange2 *me, BSTR *out) { return r_gettext2(me, 0, out); }

static HRESULT STDMETHODCALLTYPE r_settext2(ITextRange2 *me, LONG flags, BSTR s)
{
    RANGE *r = R(me);
    LONG a, b;
    (void)flags;
    LIVE(r);
    get(r, &a, &b);
    return edit(r, a, b, s ? s : L"", s ? (LONG)SysStringLen(s) : 0);
}

static HRESULT STDMETHODCALLTYPE r_settext(ITextRange2 *me, BSTR s) { return r_settext2(me, 0, s); }

static HRESULT STDMETHODCALLTYPE r_getchar2(ITextRange2 *me, LONG *ch, LONG offset)
{
    RANGE *r = R(me);
    LONG s, e;
    if (!ch) return E_INVALIDARG;
    *ch = 0;
    LIVE(r);
    get(r, &s, &e);
    if (s + offset < 0 || s + offset >= story_len(r->t)) return S_FALSE;
    *ch = char_at(r->t, s + offset);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_getchar(ITextRange2 *me, LONG *ch) { return r_getchar2(me, ch, 0); }

static HRESULT STDMETHODCALLTYPE r_setchar(ITextRange2 *me, LONG ch)
{
    RANGE *r = R(me);
    LONG s, e;
    WCHAR c = (WCHAR)ch;
    HRESULT hr;
    LIVE(r);
    get(r, &s, &e);
    if (s >= r->t->len) return E_INVALIDARG;
    hr = edit(r, s, s + 1, &c, 1);
    if (SUCCEEDED(hr)) put(r, s, e);
    return hr;
}

static HRESULT STDMETHODCALLTYPE r_dup2(ITextRange2 *me, ITextRange2 **out)
{
    RANGE *r = R(me), *d;
    LONG s, e;
    if (!out) return E_INVALIDARG;
    *out = 0;
    LIVE(r);
    get(r, &s, &e);
    if (!(d = new_range(r->t, s, e, FALSE))) return E_OUTOFMEMORY;
    *out = &d->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_dup(ITextRange2 *me, ITextRange **out) { return r_dup2(me, out); }

static HRESULT STDMETHODCALLTYPE r_setformatted2(ITextRange2 *me, ITextRange2 *other)
{
    RANGE *r = R(me), *o = as_range(other);
    BSTR b;
    HRESULT hr;
    LONG s, e;
    LIVE(r);
    if (!o || !o->t) return E_INVALIDARG;
    get(o, &s, &e);
    if (!(b = text_of(o->t, s, e, 0))) return E_OUTOFMEMORY;
    hr = r_settext(me, b);
    SysFreeString(b);
    return hr;
}

static HRESULT STDMETHODCALLTYPE r_setformatted(ITextRange2 *me, ITextRange *other) { return r_setformatted2(me, other); }

static HRESULT STDMETHODCALLTYPE r_getstart(ITextRange2 *me, LONG *cp)
{
    RANGE *r = R(me);
    LONG e;
    if (!cp) return E_INVALIDARG;
    LIVE(r);
    get(r, cp, &e);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_getend(ITextRange2 *me, LONG *cp)
{
    RANGE *r = R(me);
    LONG s;
    if (!cp) return E_INVALIDARG;
    LIVE(r);
    get(r, &s, cp);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_setstart(ITextRange2 *me, LONG cp)
{
    RANGE *r = R(me);
    LONG s, e;
    LIVE(r);
    get(r, &s, &e);
    cp = clamp(cp, lim_of(r));
    if (cp == s) return S_FALSE;
    put(r, cp, cp > e ? cp : e);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_setend(ITextRange2 *me, LONG cp)
{
    RANGE *r = R(me);
    LONG s, e;
    LIVE(r);
    get(r, &s, &e);
    cp = clamp(cp, lim_of(r));
    if (cp == e) return S_FALSE;
    put(r, cp < s ? cp : s, cp);
    return S_OK;
}

/* character and paragraph formatting objects (tomfmt.c), and the rows of tables */
static HRESULT STDMETHODCALLTYPE r_getobj(ITextRange2 *me, void **out) { (void)me; if (out) *out = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE r_getfont(ITextRange2 *me, void **out) { (void)me; return tom_format(TRUE, out); }
static HRESULT STDMETHODCALLTYPE r_getpara(ITextRange2 *me, void **out) { (void)me; return tom_format(FALSE, out); }
static HRESULT STDMETHODCALLTYPE r_setobj(ITextRange2 *me, void *p) { (void)me; (void)p; return E_NOTIMPL; }

static HRESULT STDMETHODCALLTYPE r_storylen(ITextRange2 *me, LONG *n)
{
    RANGE *r = R(me);
    if (!n) return E_INVALIDARG;
    LIVE(r);
    *n = story_len(r->t);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_storytype(ITextRange2 *me, LONG *v)
{
    if (!v) return E_INVALIDARG;
    LIVE(R(me));
    *v = tomMainTextStory;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_collapse(ITextRange2 *me, LONG start)
{
    RANGE *r = R(me);
    LONG s, e;
    LIVE(r);
    get(r, &s, &e);
    if (s == e) return S_FALSE;
    if (start) put(r, s, s); else put(r, e, e);   /* (tomTrue or tomStart: the start) */
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_expand(ITextRange2 *me, LONG unit, LONG *delta)
{
    RANGE *r = R(me);
    TS *t;
    LONG s, e, ns, ne;
    if (delta) *delta = 0;
    LIVE(r);
    t = r->t;
    if (!unit_ok(unit)) return E_NOTIMPL;
    get(r, &s, &e);
    ns = unit_start(t, unit, s);
    ne = (e > s && boundary(t, unit, e)) ? e : next_b(t, unit, e > s ? e : s);
    ne = clamp(ne, lim_of(r));
    if (ns == s && ne == e) return S_FALSE;
    put(r, ns, ne);
    if (delta) *delta = (ne - ns) - (e - s);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_getindex(ITextRange2 *me, LONG unit, LONG *index)
{
    RANGE *r = R(me);
    LONG s, e, p, n = 1;
    if (!index) return E_INVALIDARG;
    *index = 0;
    LIVE(r);
    if (!unit_ok(unit)) return E_NOTIMPL;
    get(r, &s, &e);
    for (p = 1; p <= s && p < story_len(r->t); p++) if (boundary(r->t, unit, p)) n++;
    *index = n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_setindex(ITextRange2 *me, LONG unit, LONG index, LONG extend)
{
    RANGE *r = R(me);
    LONG p = 0, i, lim;
    LIVE(r);
    if (!unit_ok(unit)) return E_NOTIMPL;
    if (index < 1) return E_INVALIDARG;
    lim = lim_of(r);
    for (i = 1; i < index; i++) {
        if (p >= lim) return E_INVALIDARG;
        p = next_b(r->t, unit, p);
    }
    if (p > lim) return E_INVALIDARG;
    put(r, p, extend ? clamp(next_b(r->t, unit, p), lim) : p);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_setrange(ITextRange2 *me, LONG anchor, LONG active)
{
    RANGE *r = R(me);
    LIVE(r);
    put_ends(r, anchor, active);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_inrange(ITextRange2 *me, ITextRange *other, LONG *b)
{
    RANGE *r = R(me), *o = as_range(other);
    LONG s, e, os, oe;
    if (b) *b = tomFalse;
    LIVE(r);
    if (!o || o->t != r->t) return S_FALSE;
    get(r, &s, &e);
    get(o, &os, &oe);
    if (s < os || e > oe) return S_FALSE;
    if (b) *b = tomTrue;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_instory(ITextRange2 *me, ITextRange *other, LONG *b)
{
    RANGE *r = R(me), *o = as_range(other);
    if (b) *b = tomFalse;
    LIVE(r);
    if (!o || o->t != r->t) return S_FALSE;
    if (b) *b = tomTrue;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_isequal(ITextRange2 *me, ITextRange *other, LONG *b)
{
    RANGE *r = R(me), *o = as_range(other);
    LONG s, e, os, oe;
    if (b) *b = tomFalse;
    LIVE(r);
    if (!o || o->t != r->t) return S_FALSE;
    get(r, &s, &e);
    get(o, &os, &oe);
    if (s != os || e != oe) return S_FALSE;
    if (b) *b = tomTrue;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_select(ITextRange2 *me)
{
    RANGE *r = R(me);
    LONG s, e;
    LIVE(r);
    get(r, &s, &e);
    if (!r->sel) set_sel(r->t, clamp(s, r->t->len), clamp(e, r->t->len));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_startof(ITextRange2 *me, LONG unit, LONG extend, LONG *delta)
{
    RANGE *r = R(me);
    LONG s, e, ns;
    if (delta) *delta = 0;
    LIVE(r);
    if (!unit_ok(unit)) return E_NOTIMPL;
    get(r, &s, &e);
    ns = unit_start(r->t, unit, s);
    put(r, ns, extend ? e : ns);
    if (delta) *delta = ns - s;
    return ns == s && (extend || s == e) ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE r_endof(ITextRange2 *me, LONG unit, LONG extend, LONG *delta)
{
    RANGE *r = R(me);
    LONG s, e, ne;
    if (delta) *delta = 0;
    LIVE(r);
    if (!unit_ok(unit)) return E_NOTIMPL;
    get(r, &s, &e);
    ne = (e > 0 && boundary(r->t, unit, e) && e > s) ? e : next_b(r->t, unit, e);
    ne = clamp(ne, lim_of(r));
    put(r, extend ? s : ne, ne);
    if (delta) *delta = ne - e;
    return ne == e && (extend || s == e) ? S_FALSE : S_OK;
}

/* Move: a range that is not an insertion point collapses toward the
 * direction first, which counts as one unit */
static HRESULT STDMETHODCALLTYPE r_move(ITextRange2 *me, LONG unit, LONG count, LONG *delta)
{
    RANGE *r = R(me);
    LONG s, e, cp, moved = 0, m = 0;
    if (delta) *delta = 0;
    LIVE(r);
    if (!unit_ok(unit)) return E_NOTIMPL;
    get(r, &s, &e);
    if (!count) return S_FALSE;
    cp = count > 0 ? e : s;
    if (s != e) { moved = count > 0 ? 1 : -1; count -= moved; }
    cp = move_by(r->t, unit, cp, count, lim_of(r), &m);
    moved += m;
    put(r, cp, cp);
    if (delta) *delta = moved;
    return moved ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE r_movestart(ITextRange2 *me, LONG unit, LONG count, LONG *delta)
{
    RANGE *r = R(me);
    LONG s, e, m = 0;
    if (delta) *delta = 0;
    LIVE(r);
    if (!unit_ok(unit)) return E_NOTIMPL;
    get(r, &s, &e);
    s = move_by(r->t, unit, s, count, lim_of(r), &m);
    put(r, s, s > e ? s : e);
    if (delta) *delta = m;
    return m ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE r_moveend(ITextRange2 *me, LONG unit, LONG count, LONG *delta)
{
    RANGE *r = R(me);
    LONG s, e, m = 0;
    if (delta) *delta = 0;
    LIVE(r);
    if (!unit_ok(unit)) return E_NOTIMPL;
    get(r, &s, &e);
    e = move_by(r->t, unit, e, count, lim_of(r), &m);
    put(r, e < s ? e : s, e);
    if (delta) *delta = m;
    return m ? S_OK : S_FALSE;
}

/* the character-set moves (MoveWhile, MoveUntil and their Start/End forms) */
static HRESULT STDMETHODCALLTYPE r_movecset(ITextRange2 *me, VARIANT *cset, LONG count, LONG *delta)
{ (void)me; (void)cset; (void)count; if (delta) *delta = 0; return E_NOTIMPL; }

static HRESULT find(RANGE *r, BSTR what, LONG count, LONG flags, LONG *length, int which)
{
    TS *t;
    LONG s, e, n, p, from, to;
    if (length) *length = 0;
    LIVE(r);
    t = r->t;
    if (!what || !(n = (LONG)SysStringLen(what))) return E_INVALIDARG;
    get(r, &s, &e);
    if (count >= 0) {                             /* forward from the start */
        from = s;
        to = count == 0 || count >= tomForward ? t->len : s + count;
        if (to > t->len) to = t->len;
        for (p = from; p + n <= to; p++) if (match_at(t, p, what, n, flags)) break;
        if (p + n > to) return S_FALSE;
    } else {                                      /* backward from the end */
        to = e > t->len ? t->len : e;
        from = count <= tomBackward ? 0 : to + count;
        if (from < 0) from = 0;
        for (p = to - n; p >= from; p--) if (match_at(t, p, what, n, flags)) break;
        if (p < from) return S_FALSE;
    }
    if (which == 0) put(r, p, p + n);
    else if (which == 1) put(r, p, e > p ? e : p);
    else put(r, s < p + n ? s : p + n, p + n);
    if (length) *length = n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_findtext(ITextRange2 *me, BSTR s, LONG c, LONG f, LONG *n) { return find(R(me), s, c, f, n, 0); }
static HRESULT STDMETHODCALLTYPE r_findstart(ITextRange2 *me, BSTR s, LONG c, LONG f, LONG *n) { return find(R(me), s, c, f, n, 1); }
static HRESULT STDMETHODCALLTYPE r_findend(ITextRange2 *me, BSTR s, LONG c, LONG f, LONG *n) { return find(R(me), s, c, f, n, 2); }

static HRESULT STDMETHODCALLTYPE r_delete(ITextRange2 *me, LONG unit, LONG count, LONG *delta)
{
    RANGE *r = R(me);
    LONG s, e, m = 0;
    HRESULT hr;
    if (delta) *delta = 0;
    LIVE(r);
    if (!unit_ok(unit)) return E_NOTIMPL;
    if (READONLY(r->t)) return E_ACCESSDENIED;
    get(r, &s, &e);
    if (s == e || (e > r->t->len && s >= r->t->len)) {
        if (!count) count = 1;
        if (count > 0) e = move_by(r->t, unit, s, count, r->t->len, &m);
        else s = move_by(r->t, unit, s, count, r->t->len, &m);
        if (s == e) return S_FALSE;
    } else m = count < 0 ? -1 : 1;
    hr = edit(r, s, e, L"", 0);
    if (delta && SUCCEEDED(hr)) *delta = m;
    return hr;
}

static HRESULT STDMETHODCALLTYPE r_copy(ITextRange2 *me, VARIANT *v)
{
    RANGE *r = R(me);
    LONG s, e;
    LIVE(r);
    if (v) VariantInit(v);
    get(r, &s, &e);
    if (r->t->bits & TXTBIT_USEPASSWORD) return E_ACCESSDENIED;
    return copy_range(r->t, s, e > r->t->len ? r->t->len : e) ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE r_cut(ITextRange2 *me, VARIANT *v)
{
    RANGE *r = R(me);
    LONG s, e;
    HRESULT hr;
    LIVE(r);
    if (READONLY(r->t)) return E_ACCESSDENIED;
    if ((hr = r_copy(me, v)) != S_OK) return hr;
    get(r, &s, &e);
    return edit(r, s, e, L"", 0);
}

static HRESULT STDMETHODCALLTYPE r_paste(ITextRange2 *me, VARIANT *v, LONG format)
{
    RANGE *r = R(me);
    LONG s, e;
    WCHAR *p;
    HRESULT hr;
    (void)v; (void)format;
    LIVE(r);
    if (READONLY(r->t)) return E_ACCESSDENIED;
    if (!(p = clipboard_text(r->t))) return S_FALSE;
    get(r, &s, &e);
    hr = edit(r, s, e, p, -1);
    release(p);
    if (SUCCEEDED(hr) && r->sel) { LONG a, b; get(r, &a, &b); set_sel(r->t, b, b); }   /* (the caret after it) */
    return hr;
}

static HRESULT STDMETHODCALLTYPE r_canpaste(ITextRange2 *me, VARIANT *v, LONG format, LONG *b)
{
    RANGE *r = R(me);
    BOOL ok;
    (void)v; (void)format;
    if (b) *b = tomFalse;
    LIVE(r);
    ok = !READONLY(r->t) && can_paste(r->t);
    if (b) *b = ok ? tomTrue : tomFalse;
    return ok ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE r_canedit(ITextRange2 *me, LONG *b)
{
    RANGE *r = R(me);
    if (b) *b = tomFalse;
    LIVE(r);
    if (b) *b = READONLY(r->t) ? tomFalse : tomTrue;
    return READONLY(r->t) ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE r_changecase(ITextRange2 *me, LONG type)
{
    RANGE *r = R(me);
    LONG s, e, i, n;
    WCHAR *buf;
    HRESULT hr;
    LIVE(r);
    if (type != tomLowerCase && type != tomUpperCase && type != tomToggleCase) return E_NOTIMPL;
    get(r, &s, &e);
    if (e > r->t->len) e = r->t->len;
    if ((n = e - s) <= 0) return S_FALSE;
    if (!(buf = alloc(n * sizeof(WCHAR)))) return E_OUTOFMEMORY;
    for (i = 0; i < n; i++) {
        WCHAR c = r->t->text[s + i], u = upper(c), l = lower(c);
        buf[i] = type == tomUpperCase ? u : type == tomLowerCase ? l : (c == u ? l : u);
    }
    if (!memcmp(buf, r->t->text + s, n * sizeof(WCHAR))) hr = S_FALSE;
    else hr = edit(r, s, e, buf, n);
    release(buf);
    return hr;
}

static HRESULT STDMETHODCALLTYPE r_getpoint(ITextRange2 *me, LONG type, LONG *x, LONG *y)
{
    RANGE *r = R(me);
    LONG s, e;
    if (!x || !y) return E_INVALIDARG;
    *x = *y = 0;
    LIVE(r);
    get(r, &s, &e);
    return point_of(r->t, (type & tomStart) ? s : e, type, x, y);
}

static HRESULT STDMETHODCALLTYPE r_setpoint(ITextRange2 *me, LONG x, LONG y, LONG type, LONG extend)
{
    RANGE *r = R(me);
    LONG s, e, anchor, active, cp;
    LIVE(r);
    cp = cp_from_point(r->t, x, y, type);
    if (!extend) { put(r, cp, cp); return S_OK; }
    get(r, &s, &e);
    get_ends(r, &anchor, &active);
    if (r->sel) put_ends(r, anchor, cp);
    else if (type & tomStart) put(r, cp, e > cp ? e : cp);
    else put(r, s < cp ? s : cp, cp);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_scroll(ITextRange2 *me, LONG value)
{
    RANGE *r = R(me);
    LONG s, e;
    LIVE(r);
    get(r, &s, &e);
    scroll_to(r->t, clamp((value & tomStart) ? s : e, r->t->len));
    redraw(r->t);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_embedded(ITextRange2 *me, IUnknown **out)
{
    if (out) *out = 0;
    LIVE(R(me));
    return S_FALSE;
}

/* ITextSelection: an ordinary range answers these too, its end being the active one */
static HRESULT STDMETHODCALLTYPE r_getflags(ITextRange2 *me, LONG *flags)
{
    RANGE *r = R(me);
    if (!flags) return E_INVALIDARG;
    *flags = 0;
    LIVE(r);
    if (r->sel && r->t->active < r->t->anchor) *flags |= tomSelStartActive;
    if (r->sel && r->t->focus) *flags |= tomSelActive;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_setflags(ITextRange2 *me, LONG flags)
{
    RANGE *r = R(me);
    LONG s, e;
    LIVE(r);
    if (!r->sel) return S_OK;
    get(r, &s, &e);
    if (flags & tomSelStartActive) set_sel(r->t, e, s);
    else set_sel(r->t, s, e);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_gettype(ITextRange2 *me, LONG *type)
{
    RANGE *r = R(me);
    LONG s, e;
    if (!type) return E_INVALIDARG;
    LIVE(r);
    get(r, &s, &e);
    *type = s == e ? tomSelectionIP : tomSelectionNormal;
    return S_OK;
}

/* Left and Right: without @extend, a selection collapses toward the side
 * first (one unit); with it, the active end moves */
static HRESULT leftright(RANGE *r, LONG unit, LONG count, LONG extend, LONG *delta)
{
    LONG s, e, anchor, active, m = 0;
    if (delta) *delta = 0;
    LIVE(r);
    if (unit != tomCharacter && unit != tomWord) return E_INVALIDARG;
    get(r, &s, &e);
    get_ends(r, &anchor, &active);
    if (!extend && s != e) {
        LONG cp = count < 0 ? s : e;
        LONG c = count < 0 ? count + 1 : count - 1;
        cp = move_by(r->t, unit, cp, c, lim_of(r), &m);
        put_ends(r, cp, cp);
        m += count < 0 ? -1 : 1;
    } else {
        active = move_by(r->t, unit, active, count, lim_of(r), &m);
        put_ends(r, extend ? anchor : active, active);
    }
    if (r->sel) r->t->targetx = -1;
    if (delta) *delta = m < 0 ? -m : m;
    return m ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE r_left(ITextRange2 *me, LONG unit, LONG count, LONG extend, LONG *delta)
{ return leftright(R(me), unit, -count, extend, delta); }
static HRESULT STDMETHODCALLTYPE r_right(ITextRange2 *me, LONG unit, LONG count, LONG extend, LONG *delta)
{ return leftright(R(me), unit, count, extend, delta); }

/* Up and Down by lines (keeping the x) or paragraphs */
static HRESULT updown(RANGE *r, LONG unit, LONG count, LONG extend, LONG *delta)
{
    TS *t;
    LONG anchor, active, m = 0;
    if (delta) *delta = 0;
    LIVE(r);
    t = r->t;
    get_ends(r, &anchor, &active);
    if (unit == tomLine) {
        int l, target;
        LONG x;
        ensure_layout(t);
        l = line_of(t, active);
        x = r->sel && t->targetx >= 0 ? t->targetx : x_of(t, active);
        target = l + count;
        if (target < 0) target = 0;
        if (target >= t->nlines) target = t->nlines - 1;
        m = target - l;
        if (m) active = cp_at_x(t, target, x);
        else active = count < 0 ? 0 : t->len;
        put_ends(r, extend ? anchor : active, active);
        if (r->sel) t->targetx = x;
    } else if (unit == tomParagraph || unit == tomStory) {
        active = move_by(t, unit, active, count, lim_of(r), &m);
        put_ends(r, extend ? anchor : active, active);
    } else return E_INVALIDARG;
    if (delta) *delta = m < 0 ? -m : m;
    return m ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE r_up(ITextRange2 *me, LONG unit, LONG count, LONG extend, LONG *delta)
{ return updown(R(me), unit, -count, extend, delta); }
static HRESULT STDMETHODCALLTYPE r_down(ITextRange2 *me, LONG unit, LONG count, LONG extend, LONG *delta)
{ return updown(R(me), unit, count, extend, delta); }

static HRESULT homeend(RANGE *r, LONG unit, LONG extend, LONG *delta, BOOL end)
{
    TS *t;
    LONG anchor, active, cp;
    int l;
    if (delta) *delta = 0;
    LIVE(r);
    t = r->t;
    get_ends(r, &anchor, &active);
    if (unit == tomStory) cp = end ? t->len : 0;
    else if (unit == tomLine) {
        ensure_layout(t);
        l = line_of(t, active);
        cp = t->lines[l].start;
        if (end) {
            cp += t->lines[l].len;
            if (l + 1 < t->nlines && cp == t->lines[l + 1].start && cp > t->lines[l].start) cp--;
        }
    } else return E_INVALIDARG;
    put_ends(r, extend ? anchor : cp, cp);
    if (r->sel) t->targetx = -1;
    if (delta) *delta = cp - active;
    return cp != active ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE r_home(ITextRange2 *me, LONG unit, LONG extend, LONG *delta) { return homeend(R(me), unit, extend, delta, FALSE); }
static HRESULT STDMETHODCALLTYPE r_end(ITextRange2 *me, LONG unit, LONG extend, LONG *delta) { return homeend(R(me), unit, extend, delta, TRUE); }

static HRESULT STDMETHODCALLTYPE r_typetext(ITextRange2 *me, BSTR s)
{
    RANGE *r = R(me);
    LONG a, b;
    HRESULT hr;
    LIVE(r);
    if (READONLY(r->t)) return E_ACCESSDENIED;
    if (r->sel) { replace_sel(r->t, s ? s : L"", s ? (LONG)SysStringLen(s) : 0, TRUE, TRUE); return S_OK; }
    get(r, &a, &b);
    hr = edit(r, a, b, s ? s : L"", s ? (LONG)SysStringLen(s) : 0);
    if (SUCCEEDED(hr)) r->start = r->end;           /* (an insertion point after it, as typing leaves) */
    return hr;
}

/* ITextRange2 */
static HRESULT STDMETHODCALLTYPE r_getcch(ITextRange2 *me, LONG *n)
{
    RANGE *r = R(me);
    LONG s, e;
    if (!n) return E_INVALIDARG;
    LIVE(r);
    get(r, &s, &e);
    *n = e - s;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_getunk(ITextRange2 *me, IUnknown **out) { (void)me; if (out) *out = 0; return E_NOTIMPL; }

static HRESULT STDMETHODCALLTYPE r_getcount(ITextRange2 *me, LONG *n)
{
    if (!n) return E_INVALIDARG;
    LIVE(R(me));
    *n = 1;                                       /* (no subranges) */
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_getformatted2(ITextRange2 *me, ITextRange2 **out) { return r_dup2(me, out); }
static HRESULT STDMETHODCALLTYPE r_getformatted(ITextRange2 *me, ITextRange **out) { return r_dup2(me, out); }

static HRESULT STDMETHODCALLTYPE r_getgravity(ITextRange2 *me, LONG *v)
{
    if (!v) return E_INVALIDARG;
    LIVE(R(me));
    *v = R(me)->gravity;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_setgravity(ITextRange2 *me, LONG v)
{
    LIVE(R(me));
    R(me)->gravity = v;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_startpara(ITextRange2 *me, LONG *cp)
{
    RANGE *r = R(me);
    LONG s, e;
    if (!cp) return E_INVALIDARG;
    LIVE(r);
    get(r, &s, &e);
    *cp = unit_start(r->t, tomParagraph, s);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_geturl(ITextRange2 *me, BSTR *out) { (void)me; if (out) *out = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE r_setbstr(ITextRange2 *me, BSTR s) { (void)me; (void)s; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE r_addsub(ITextRange2 *me, LONG a, LONG b, LONG c) { (void)me; (void)a; (void)b; (void)c; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE r_long1(ITextRange2 *me, LONG a) { (void)me; (void)a; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE r_long2(ITextRange2 *me, LONG a, LONG b) { (void)me; (void)a; (void)b; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE r_find2(ITextRange2 *me, ITextRange2 *o, LONG c, LONG f, LONG *d)
{ (void)me; (void)o; (void)c; (void)f; if (d) *d = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE r_dropcap(ITextRange2 *me, LONG *a, LONG *b) { (void)me; if (a) *a = 0; if (b) *b = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE r_getinline(ITextRange2 *me, LONG *a, LONG *b, LONG *c, LONG *d, LONG *e, LONG *f, LONG *g, LONG *h, LONG *i)
{ (void)me; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE r_getprop(ITextRange2 *me, LONG type, LONG *v) { (void)me; (void)type; if (v) *v = 0; return E_INVALIDARG; }
static HRESULT STDMETHODCALLTYPE r_setprop(ITextRange2 *me, LONG type, LONG v) { (void)me; (void)type; (void)v; return E_INVALIDARG; }

static HRESULT STDMETHODCALLTYPE r_getrect(ITextRange2 *me, LONG type, LONG *left, LONG *top, LONG *right, LONG *bottom, LONG *hit_)
{
    RANGE *r = R(me);
    LONG s, e, x1, y1, x2, y2, flags = (type & (tomClientCoord | tomAllowOffClient)) | tomAllowOffClient;
    if (hit_) *hit_ = 0;
    LIVE(r);
    get(r, &s, &e);
    point_of(r->t, s, flags, &x1, &y1);
    point_of(r->t, e, flags | TA_BOTTOM_, &x2, &y2);
    if (r->t->nlines > 0 && line_of(r->t, s > r->t->len ? r->t->len : s) != line_of(r->t, e > r->t->len ? r->t->len : e)) {
        RECT rc;
        POINT a, b;
        client_rect(r->t, &rc);
        a.x = rc.left; a.y = 0; b.x = rc.right; b.y = 0;
        if (!(type & tomClientCoord) && r->t->host) {
            HOST(r->t)->TxClientToScreen(r->t->host, &a);
            HOST(r->t)->TxClientToScreen(r->t->host, &b);
        }
        x1 = a.x;
        x2 = b.x;
    }
    if (left) *left = x1;
    if (top) *top = y1;
    if (right) *right = x2;
    if (bottom) *bottom = y2;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_getsub(ITextRange2 *me, LONG i, LONG *first, LONG *lim)
{
    RANGE *r = R(me);
    LONG anchor, active;
    LIVE(r);
    if (i != 0) return E_INVALIDARG;
    get_ends(r, &anchor, &active);
    if (first) *first = active;
    if (lim) *lim = anchor;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_void(ITextRange2 *me) { (void)me; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE r_long3(ITextRange2 *me, LONG a, LONG b, LONG c) { (void)me; (void)a; (void)b; (void)c; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE r_setsub(ITextRange2 *me, LONG anchor, LONG active)
{
    LIVE(R(me));
    put_ends(R(me), anchor, active);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE r_setinline(ITextRange2 *me, LONG a, LONG b, LONG c, LONG d, LONG e, LONG f, LONG g, LONG h)
{ (void)me; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE r_mathtype(ITextRange2 *me, BSTR s, LONG *v) { (void)me; (void)s; if (v) *v = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE r_image(ITextRange2 *me, LONG w, LONG h, LONG a, LONG ty, BSTR alt, IStream *st)
{ (void)me; (void)w; (void)h; (void)a; (void)ty; (void)alt; (void)st; return E_NOTIMPL; }

static const ITextRange2Vtbl range_vtbl = {
    r_qi, r_addref, r_release,
    r_tcount, r_tinfo, r_ids, r_invoke,
    /* ITextRange */
    r_gettext, r_settext, r_getchar, r_setchar, r_dup, r_getformatted, r_setformatted,
    r_getstart, r_setstart, r_getend, r_setend, r_getfont, r_setobj, r_getpara, r_setobj,
    r_storylen, r_storytype, r_collapse, r_expand, r_getindex, r_setindex, r_setrange,
    r_inrange, r_instory, r_isequal, r_select, r_startof, r_endof, r_move, r_movestart, r_moveend,
    r_movecset, r_movecset, r_movecset, r_movecset, r_movecset, r_movecset,
    r_findtext, r_findstart, r_findend, r_delete, r_cut, r_copy, r_paste, r_canpaste, r_canedit,
    r_changecase, r_getpoint, r_setpoint, r_scroll, r_embedded,
    /* ITextSelection */
    r_getflags, r_setflags, r_gettype, r_left, r_right, r_up, r_down, r_home, r_end, r_typetext,
    /* ITextRange2 */
    r_getcch, r_getunk, r_getunk, r_getcount, r_dup2, r_getfont, r_setobj, r_getformatted2, r_setformatted2,
    r_getgravity, r_setgravity, r_getpara, r_setobj, r_getobj, r_startpara, r_getunk, r_geturl, r_setbstr,
    r_addsub, r_long1, r_long2, r_find2, r_getchar2, r_dropcap, r_getinline, r_getprop, r_getrect,
    r_getsub, r_gettext2, r_void, r_long3, r_long1, r_setsub, r_long2, r_setprop, r_settext2, r_void,
    r_setinline, r_mathtype, r_image,
};

/* ---- ITextDocument2 ------------------------------------------------------ */
static HRESULT STDMETHODCALLTYPE d_qi(ITextDocument2 *me, REFIID iid, void **out)
{ TS *t = DOC(me); return t->outer->lpVtbl->QueryInterface(t->outer, iid, out); }
static ULONG STDMETHODCALLTYPE d_addref(ITextDocument2 *me) { TS *t = DOC(me); return t->outer->lpVtbl->AddRef(t->outer); }
static ULONG STDMETHODCALLTYPE d_release(ITextDocument2 *me) { TS *t = DOC(me); return t->outer->lpVtbl->Release(t->outer); }

static HRESULT STDMETHODCALLTYPE d_tcount(ITextDocument2 *me, UINT *n) { (void)me; if (n) *n = 0; return S_OK; }
static HRESULT STDMETHODCALLTYPE d_tinfo(ITextDocument2 *me, UINT i, LCID l, void **o) { (void)me; (void)i; (void)l; if (o) *o = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_ids(ITextDocument2 *me, REFIID i, LPOLESTR *n, UINT c, LCID l, LONG *d)
{ (void)me; (void)i; (void)n; (void)c; (void)l; (void)d; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_invoke(ITextDocument2 *me, LONG id, REFIID i, LCID l, WORD f, void *p, VARIANT *v, void *e, UINT *a)
{ (void)me; (void)id; (void)i; (void)l; (void)f; (void)p; (void)v; (void)e; (void)a; return E_NOTIMPL; }

static HRESULT STDMETHODCALLTYPE d_getname(ITextDocument2 *me, BSTR *out) { (void)me; if (!out) return E_INVALIDARG; *out = 0; return S_FALSE; }

static HRESULT STDMETHODCALLTYPE d_getselection2(ITextDocument2 *me, ITextSelection2 **out)
{
    TS *t = DOC(me);
    if (!out) return E_INVALIDARG;
    *out = 0;
    if (!t->selobj && !(t->selobj = new_range(t, 0, 0, TRUE))) return E_OUTOFMEMORY;
    InterlockedIncrement(&t->selobj->ref);
    *out = &t->selobj->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_getselection(ITextDocument2 *me, ITextSelection **out) { return d_getselection2(me, out); }

static HRESULT STDMETHODCALLTYPE d_storycount(ITextDocument2 *me, LONG *n) { (void)me; if (!n) return E_INVALIDARG; *n = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE d_getunk(ITextDocument2 *me, IUnknown **out) { (void)me; if (out) *out = 0; return E_NOTIMPL; }

static HRESULT STDMETHODCALLTYPE d_getsaved(ITextDocument2 *me, LONG *v)
{
    if (!v) return E_INVALIDARG;
    *v = DOC(me)->modified ? tomFalse : tomTrue;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_setsaved(ITextDocument2 *me, LONG v) { DOC(me)->modified = v == tomFalse; return S_OK; }
static HRESULT STDMETHODCALLTYPE d_gettab(ITextDocument2 *me, float *v) { if (!v) return E_INVALIDARG; *v = DOC(me)->tabstop; return S_OK; }

static HRESULT STDMETHODCALLTYPE d_settab(ITextDocument2 *me, float v)
{
    if (v <= 0) return E_INVALIDARG;
    DOC(me)->tabstop = v;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_new(ITextDocument2 *me) { set_text(DOC(me), L"", 0); return S_OK; }
static HRESULT STDMETHODCALLTYPE d_opensave(ITextDocument2 *me, VARIANT *v, LONG f, LONG cp) { (void)me; (void)v; (void)f; (void)cp; return E_NOTIMPL; }

static HRESULT STDMETHODCALLTYPE d_freeze(ITextDocument2 *me, LONG *count)
{
    TS *t = DOC(me);
    t->frozen++;
    if (count) *count = t->frozen;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_unfreeze(ITextDocument2 *me, LONG *count)
{
    TS *t = DOC(me);
    if (t->frozen > 0 && !--t->frozen) redraw(t);
    if (count) *count = t->frozen;
    return t->frozen ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE d_void(ITextDocument2 *me) { (void)me; return E_NOTIMPL; }

/* Undo and Redo @count steps; tomSuspend and tomResume stop and restart
 * recording, tomFalse turns undo off (and empties it), tomTrue on */
static HRESULT undoredo(TS *t, LONG count, LONG *done, BOOL redo)
{
    LONG n = 0;
    if (done) *done = 0;
    if (!redo) {
        switch (count) {
        case tomSuspend: t->undosuspend = TRUE; return S_OK;
        case tomResume: t->undosuspend = FALSE; return S_OK;
        case tomFalse:
            t->undooff = TRUE;
            t->typing = FALSE;
            clear_undo(t);
            return S_OK;
        case tomTrue: t->undooff = FALSE; return S_OK;
        }
    }
    if (count < 0) return E_INVALIDARG;
    while (n < count && !READONLY(t) && undo_redo(t, redo)) n++;
    if (done) *done = n;
    return n == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE d_undo(ITextDocument2 *me, LONG count, LONG *done) { return undoredo(DOC(me), count, done, FALSE); }
static HRESULT STDMETHODCALLTYPE d_redo(ITextDocument2 *me, LONG count, LONG *done) { return undoredo(DOC(me), count, done, TRUE); }

static HRESULT STDMETHODCALLTYPE d_range2(ITextDocument2 *me, LONG active, LONG anchor, ITextRange2 **out)
{
    TS *t = DOC(me);
    RANGE *r;
    if (!out) return E_INVALIDARG;
    *out = 0;
    if (!(r = new_range(t, anchor, active, FALSE))) return E_OUTOFMEMORY;
    *out = &r->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_range(ITextDocument2 *me, LONG active, LONG anchor, ITextRange **out)
{ return d_range2(me, active, anchor, out); }

static HRESULT STDMETHODCALLTYPE d_frompoint2(ITextDocument2 *me, LONG x, LONG y, LONG type, ITextRange2 **out)
{
    TS *t = DOC(me);
    LONG cp = cp_from_point(t, x, y, type);
    return d_range2(me, cp, cp, out);
}

static HRESULT STDMETHODCALLTYPE d_frompoint(ITextDocument2 *me, LONG x, LONG y, ITextRange **out)
{ return d_frompoint2(me, x, y, 0, out); }

static HRESULT STDMETHODCALLTYPE d_getlong(ITextDocument2 *me, LONG *v) { (void)me; if (v) *v = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_setlong(ITextDocument2 *me, LONG v) { (void)me; (void)v; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_long2(ITextDocument2 *me, LONG a, LONG b) { (void)me; (void)a; (void)b; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_getobj(ITextDocument2 *me, void **out) { (void)me; if (out) *out = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_setobj(ITextDocument2 *me, void *p) { (void)me; (void)p; return E_NOTIMPL; }

static HRESULT STDMETHODCALLTYPE d_getcaret(ITextDocument2 *me, LONG *v) { if (!v) return E_INVALIDARG; *v = DOC(me)->carettype; return S_OK; }
static HRESULT STDMETHODCALLTYPE d_setcaret(ITextDocument2 *me, LONG v) { DOC(me)->carettype = v; return S_OK; }

static HRESULT STDMETHODCALLTYPE d_eaflags(ITextDocument2 *me, LONG *v)
{
    TS *t = DOC(me);
    ITextHost2 *h2 = 0;
    static const IID iid_host2 = NOVA_IID_ITextHost2;
    if (!v) return E_INVALIDARG;
    *v = 0;
    if (t->host && HOST(t)->QueryInterface(t->host, &iid_host2, (void **)&h2) == S_OK && h2) {
        h2->lpVtbl->TxGetEastAsianFlags(h2, v);
        h2->lpVtbl->Release(h2);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_generator(ITextDocument2 *me, BSTR *out)
{
    (void)me;
    if (!out) return E_INVALIDARG;
    *out = SysAllocString(L"NovaOS Rich Edit");
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE d_ime(ITextDocument2 *me, LONG v) { (void)me; (void)v; return S_OK; }
static HRESULT STDMETHODCALLTYPE d_getnotify(ITextDocument2 *me, LONG *v) { if (!v) return E_INVALIDARG; *v = DOC(me)->notifymode; return S_OK; }
static HRESULT STDMETHODCALLTYPE d_setnotify(ITextDocument2 *me, LONG v) { DOC(me)->notifymode = v; return S_OK; }
static HRESULT STDMETHODCALLTYPE d_gettypo(ITextDocument2 *me, LONG *v) { if (!v) return E_INVALIDARG; *v = DOC(me)->typography; return S_OK; }

static HRESULT STDMETHODCALLTYPE d_settypo(ITextDocument2 *me, LONG options, LONG mask)
{
    TS *t = DOC(me);
    t->typography = (t->typography & ~mask) | (options & mask);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_version(ITextDocument2 *me, LONG *v) { (void)me; if (!v) return E_INVALIDARG; *v = 8 << 16; return S_OK; }

static HRESULT STDMETHODCALLTYPE d_window(ITextDocument2 *me, LONGLONG *v)
{
    if (!v) return E_INVALIDARG;
    *v = (LONGLONG)(LONG_PTR)host_window(DOC(me));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_msgfilter(ITextDocument2 *me, IUnknown *f) { (void)me; (void)f; return E_NOTIMPL; }

static HRESULT STDMETHODCALLTYPE d_checklimit(ITextDocument2 *me, LONG cch, LONG *exceed)
{
    TS *t = DOC(me);
    LONG over = t->len + cch - (LONG)t->maxlen;
    if (!exceed) return E_INVALIDARG;
    *exceed = over > 0 ? over : 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_clientrect(ITextDocument2 *me, LONG type, LONG *left, LONG *top, LONG *right, LONG *bottom)
{
    TS *t = DOC(me);
    RECT rc;
    POINT a, b;
    client_rect(t, &rc);
    a.x = rc.left; a.y = rc.top; b.x = rc.right; b.y = rc.bottom;
    if (!(type & tomClientCoord) && t->host) {
        HOST(t)->TxClientToScreen(t->host, &a);
        HOST(t)->TxClientToScreen(t->host, &b);
    }
    if (left) *left = a.x;
    if (top) *top = a.y;
    if (right) *right = b.x;
    if (bottom) *bottom = b.y;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_geteffect(ITextDocument2 *me, LONG i, LONG *v) { (void)me; (void)i; if (v) *v = 0; return E_NOTIMPL; }

static HRESULT STDMETHODCALLTYPE d_getimm(ITextDocument2 *me, LONGLONG *v)
{
    TS *t = DOC(me);
    if (!v) return E_INVALIDARG;
    *v = t->host ? (LONGLONG)(LONG_PTR)HOST(t)->TxImmGetContext(t->host) : 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_releaseimm(ITextDocument2 *me, LONGLONG v)
{
    TS *t = DOC(me);
    if (t->host) HOST(t)->TxImmReleaseContext(t->host, (HIMC_)(LONG_PTR)v);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d_preferred(ITextDocument2 *me, LONG cp, LONG cr, LONG o, LONG ccr, LONG cs, BSTR *n, LONG *p, LONG *s)
{
    (void)me; (void)cp; (void)cr; (void)o; (void)ccr; (void)cs;
    if (n) *n = 0;
    if (p) *p = 0;
    if (s) *s = 0;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d_getprop(ITextDocument2 *me, LONG type, LONG *v) { (void)me; (void)type; if (v) *v = 0; return E_INVALIDARG; }
static HRESULT STDMETHODCALLTYPE d_setprop(ITextDocument2 *me, LONG type, LONG v) { (void)me; (void)type; (void)v; return E_INVALIDARG; }
static HRESULT STDMETHODCALLTYPE d_notify(ITextDocument2 *me, LONG v) { (void)me; (void)v; return S_OK; }
static HRESULT STDMETHODCALLTYPE d_releasecm(ITextDocument2 *me, IUnknown *p) { (void)me; (void)p; return S_OK; }
static HRESULT STDMETHODCALLTYPE d_beep(ITextDocument2 *me) { (void)me; MessageBeep(0); return S_OK; }
static HRESULT STDMETHODCALLTYPE d_update(ITextDocument2 *me, LONG v) { (void)v; redraw(DOC(me)); return S_OK; }
static HRESULT STDMETHODCALLTYPE d_updatewindow(ITextDocument2 *me) { redraw(DOC(me)); return S_OK; }
static HRESULT STDMETHODCALLTYPE d_setunk(ITextDocument2 *me, IUnknown *p) { (void)me; (void)p; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE d_story(ITextDocument2 *me, LONG i, IUnknown **out) { (void)me; (void)i; if (out) *out = 0; return E_NOTIMPL; }

static const ITextDocument2Vtbl doc_vtbl = {
    d_qi, d_addref, d_release,
    d_tcount, d_tinfo, d_ids, d_invoke,
    /* ITextDocument */
    d_getname, d_getselection, d_storycount, d_getunk, d_getsaved, d_setsaved, d_gettab, d_settab,
    d_new, d_opensave, d_opensave, d_freeze, d_unfreeze, d_void, d_void, d_undo, d_redo, d_range, d_frompoint,
    /* ITextDocument2 */
    d_getcaret, d_setcaret, d_getunk, d_getobj, d_setobj, d_getobj, d_setobj, d_eaflags, d_generator,
    d_ime, d_getnotify, d_setnotify, d_getselection2, d_getunk, d_gettypo, d_version, d_window,
    d_msgfilter, d_checklimit, d_getunk, d_clientrect, d_geteffect, d_getimm, d_preferred, d_getprop,
    d_getunk, d_notify, d_range2, d_frompoint2, d_releasecm, d_releaseimm, d_long2, d_setprop,
    d_settypo, d_beep, d_update, d_updatewindow, d_getlong, d_long2, d_getunk, d_setunk, d_getunk,
    d_getunk, d_story,
};

/* ---- IRichEditOle -------------------------------------------------------- */
static HRESULT STDMETHODCALLTYPE o_qi(IRichEditOle_ *me, REFIID iid, void **out)
{ TS *t = REOLE(me); return t->outer->lpVtbl->QueryInterface(t->outer, iid, out); }
static ULONG STDMETHODCALLTYPE o_addref(IRichEditOle_ *me) { TS *t = REOLE(me); return t->outer->lpVtbl->AddRef(t->outer); }
static ULONG STDMETHODCALLTYPE o_release(IRichEditOle_ *me) { TS *t = REOLE(me); return t->outer->lpVtbl->Release(t->outer); }
static HRESULT STDMETHODCALLTYPE o_site(IRichEditOle_ *me, IUnknown **out) { (void)me; if (out) *out = 0; return E_NOTIMPL; }
static LONG STDMETHODCALLTYPE o_count(IRichEditOle_ *me) { (void)me; return 0; }
static HRESULT STDMETHODCALLTYPE o_getobject(IRichEditOle_ *me, LONG i, void *o, DWORD f) { (void)me; (void)i; (void)o; (void)f; return E_INVALIDARG; }
static HRESULT STDMETHODCALLTYPE o_insert(IRichEditOle_ *me, void *o) { (void)me; (void)o; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE o_convert(IRichEditOle_ *me, LONG i, REFCLSID c, LPCSTR s) { (void)me; (void)i; (void)c; (void)s; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE o_activateas(IRichEditOle_ *me, REFCLSID a, REFCLSID b) { (void)me; (void)a; (void)b; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE o_hostnames(IRichEditOle_ *me, LPCSTR a, LPCSTR b) { (void)me; (void)a; (void)b; return S_OK; }
static HRESULT STDMETHODCALLTYPE o_linkavail(IRichEditOle_ *me, LONG i, BOOL b) { (void)me; (void)i; (void)b; return E_INVALIDARG; }
static HRESULT STDMETHODCALLTYPE o_aspect(IRichEditOle_ *me, LONG i, DWORD a) { (void)me; (void)i; (void)a; return E_INVALIDARG; }
static HRESULT STDMETHODCALLTYPE o_handsoff(IRichEditOle_ *me, LONG i) { (void)me; (void)i; return E_INVALIDARG; }
static HRESULT STDMETHODCALLTYPE o_saved(IRichEditOle_ *me, LONG i, IUnknown *s) { (void)me; (void)i; (void)s; return E_INVALIDARG; }
static HRESULT STDMETHODCALLTYPE o_deactivate(IRichEditOle_ *me) { (void)me; return S_OK; }
static HRESULT STDMETHODCALLTYPE o_help(IRichEditOle_ *me, BOOL b) { (void)me; (void)b; return S_OK; }
static HRESULT STDMETHODCALLTYPE o_clipdata(IRichEditOle_ *me, CHARRANGE *c, DWORD r, IUnknown **out) { (void)me; (void)c; (void)r; if (out) *out = 0; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE o_import(IRichEditOle_ *me, IUnknown *d, WORD cf, HGLOBAL g) { (void)me; (void)d; (void)cf; (void)g; return E_NOTIMPL; }

static const IRichEditOle_Vtbl reole_vtbl = {
    o_qi, o_addref, o_release, o_site, o_count, o_count, o_getobject, o_insert, o_convert, o_activateas,
    o_hostnames, o_linkavail, o_aspect, o_handsoff, o_saved, o_deactivate, o_help, o_clipdata, o_import,
};

/* ---- the text services' side --------------------------------------------- */
void tom_init(TS *t)
{
    t->doc.lpVtbl = &doc_vtbl;
    t->reole.lpVtbl = &reole_vtbl;
    t->tabstop = 36.0f;                           /* (half an inch, in points) */
    t->carettype = tomCaretTypeNormal;
}

/* the text services are going: ranges still held answer CO_E_RELEASED */
void tom_free(TS *t)
{
    RANGE *r = t->ranges, *next;
    for (; r; r = next) { next = r->next; r->t = 0; r->next = 0; }
    t->ranges = 0;
    if (t->selobj) {
        RANGE *s = t->selobj;
        t->selobj = 0;
        s->t = 0;
        r_release(&s->iface);
    }
}

HRESULT tom_qi(TS *t, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &iid_dispatch) || IsEqualGUID(iid, &iid_doc) || IsEqualGUID(iid, &iid_doc2)) *out = &t->doc;
    else if (IsEqualGUID(iid, &iid_reole)) *out = &t->reole;
    else return E_NOINTERFACE;
    t->outer->lpVtbl->AddRef(t->outer);
    return S_OK;
}
