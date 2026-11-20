/* tsint.h — the windowless Rich Edit's object, shared by textserv.c (the
 * text services) and tom.c (the Text Object Model over the same text) */
#pragma once
#include <novatextserv.h>
#include <winstring.h>

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);

#define UNDO_MAX    100
#define DEFAULT_MAX 32767                         /* Rich Edit's text limit until EM_EXLIMITTEXT */

typedef struct { WCHAR *text; BYTE *fmt; LONG len, anchor, active; } SNAP;
/* a character format of rich text (runs.c): the default's properties that
 * it sets (mask: CFM_BOLD, CFM_ITALIC, CFM_UNDERLINE, CFM_STRIKEOUT,
 * CFM_COLOR) and their values (CFE_ bits, the same as the CFM_ ones) */
typedef struct { DWORD mask, effects; COLORREF color; } RUNFMT;
#define MAXFMT 255
#define FMT_MASK (CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT | CFM_COLOR)
typedef struct { LONG start, len, width; } LINE;  /* a laid-out line: [start, start+len) */

typedef struct TS {
    IUnknown inner;                               /* the object's own IUnknown (aggregation) */
    ITextServices2 iface;
    LONG ref;
    IUnknown *outer;
    ITextHost *host;                              /* not AddRef'd, as Windows' isn't */
    WCHAR *text;
    BYTE *fmt;                                    /* each character's format (runs.c), an index into fmts */
    LONG len, cap;
    LONG anchor, active;                          /* the selection: active end has the caret */
    DWORD bits;                                   /* TXTBIT_* from the host */
    DWORD maxlen, mask, options, editstyle, langopts, textmode;
    WCHAR pwch;                                   /* the password character */
    BOOL modified, focus, inplace, dragging, typing, caret;
    SNAP undo[UNDO_MAX], redo[UNDO_MAX];
    int nundo, nredo, undolimit;
    CHARFORMAT2W cf;                              /* the default character format */
    HFONT font;
    LONG lineh, ascent;
    LINE *lines;
    int nlines, linecap;
    LONG layoutw;                                 /* the width laid out for (0: no wrap) */
    BOOL dirty;
    LONG scrollx, scrolly;
    LONG targetx;                                 /* Up and Down keep to this x */
    RECT client;
    HDC drawdc, memdc;
    int mono;                                     /* TxDrawD2D's passes: 1 colour on black, 2 all white */
    /* the Text Object Model (tom.c) */
    ITextDocument2 doc;                           /* (QueryInterface on the text services) */
    IRichEditOle_ reole;                          /* (EM_GETOLEINTERFACE) */
    struct RANGE *ranges;                         /* the live ranges, which follow edits */
    struct RANGE *selobj;                         /* the selection's object, made when asked */
    LONG frozen;                                  /* ITextDocument::Freeze count: no drawing */
    BOOL undooff, undosuspend;                    /* Undo(tomFalse), Undo(tomSuspend) */
    float tabstop;
    LONG carettype, notifymode, typography;
    const void *wdoc;                             /* Microsoft.UI.Text's ITextDocument (winrtdoc.c) */
    /* DirectWrite's fonts (dwtext.c), when the host gives them (IProvideFontInfo) */
    void *fontinfo;                               /* (the host's, held without a reference, as the host) */
    BOOL fontinfo_asked;
    IUnknown *face;                               /* the default format's IDWriteFontFace, or none: GDI */
    float emsize;                                 /* its size in pixels */
    /* rich text's character formats (runs.c) */
    RUNFMT fmts[MAXFMT];                          /* (fmts[0]: the default's, nothing set) */
    int nfmts;
    int insfmt;                                   /* the format typing at the caret takes, or -1 */
    DWORD accels;                                 /* the formatting keys allowed (EM_SETFORMATACCELERATORS) */
    HFONT fonts[4];                               /* GDI's fonts by key (bold 1, italic 2) */
    IUnknown *faces[4];                           /* DirectWrite's faces by key */
    /* the context menu (XAML text input, part 6) */
    IRichEditOleCallback_ *olecb;                 /* EM_SETOLECALLBACK's, held */
    DWORD touchopts;                              /* EM_SETTOUCHOPTIONS' (RTO_DISABLECONTEXTMENU) */
} TS;

#define FROM_IFACE(p) ((TS *)((char *)(p) - __builtin_offsetof(TS, iface)))
#define FROM_INNER(p) ((TS *)((char *)(p) - __builtin_offsetof(TS, inner)))


#define HOST(t) ((t)->host->lpVtbl)

static inline LONG selmin(TS *t) { return t->anchor < t->active ? t->anchor : t->active; }
static inline LONG selmax(TS *t) { return t->anchor < t->active ? t->active : t->anchor; }

/* textserv.c */
void *alloc(SIZE_T n);
void release(void *p);
void ensure_layout(TS *t);
int line_of(TS *t, LONG cp);
LONG x_of(TS *t, LONG cp);
LONG cp_at_x(TS *t, int l, LONG x);
LONG hit(TS *t, LONG x, LONG y);
void client_rect(TS *t, RECT *rc);
void scroll_to(TS *t, LONG cp);
void redraw(TS *t);
void selection_changed(TS *t);
void set_sel(TS *t, LONG anchor, LONG active);
void changed(TS *t);
BOOL undo_redo(TS *t, BOOL redo);
void save_undo(TS *t);
void set_default_format(TS *t, const CHARFORMATW *cf);
void measure(TS *t, HDC dc, LONG start, LONG n, int *dx, SIZE *sz);
void clear_undo(TS *t);
LONG splice(TS *t, LONG a, LONG b, const WCHAR *s, LONG n, BOOL undoable, BOOL typed);
LONG replace_sel(TS *t, const WCHAR *s, LONG n, BOOL undoable, BOOL typed);
void set_text(TS *t, const WCHAR *s, LONG n);
BOOL is_word(WCHAR c);
BOOL copy_range(TS *t, LONG a, LONG b);
BOOL can_paste(TS *t);
WCHAR *clipboard_text(TS *t);
HWND host_window(TS *t);
/* WinUIEdit's requests to the host: a cut, copy or paste the user asked for
 * (data: a bool the host sets when it handled it) */
#define EN_PASTEREQUESTED_  0x0730
#define EN_COPYREQUESTED_   0x0731
#define EN_CUTREQUESTED_    0x0732
BOOL host_handles(TS *t, DWORD code);

/* dwtext.c */
void dw_refresh(TS *t);
void dw_free(TS *t);
BOOL dw_measure(TS *t, int key, const WCHAR *s, LONG n, int *dx, SIZE *sz);
HRESULT dw_draw(TS *t, void *rt, const RECT *rc, BOOL opaque);
IUnknown *dw_face(TS *t, int key);                /* the face of a font key (fmt_key), or null */

/* tom.c */
void tom_init(TS *t);
void tom_free(TS *t);
HRESULT tom_qi(TS *t, REFIID iid, void **out);
void tom_edited(TS *t, LONG a, LONG b, LONG k);   /* [a, b) became k characters */
void tom_clamp(TS *t);                            /* the text was replaced */

/* winrtdoc.c */
void *wdoc_qi(TS *t, REFIID iid);                 /* the Windows Runtime document, or null */
HRESULT make_hstring(const WCHAR *s, UINT32 n, HSTRING *out);

/* winrtfmt.c */
HRESULT wfmt_wrap(void *tomfont, void **out);     /* Microsoft.UI.Text's ITextCharacterFormat over a TOM font */
void *wfmt_tom(void *format);                     /* the TOM font of one of ours, or null */

TS *tom_range_ts(ITextRange2 *r);                 /* a range's text services (null once gone, or not ours) */

/* tomfmt.c */
HRESULT tom_format(BOOL font, ITextRange2 *range, void **out);   /* a range's ITextFont2 (or ITextPara2) */
HRESULT tom_set_font(ITextRange2 *range, void *font);            /* ITextRange::SetFont */

/* runs.c */
#define RICH(t) (((t)->bits & TXTBIT_RICHTEXT) != 0)
BOOL fmt_reserve(TS *t, LONG cap);                /* the formats' room, as the text's */
int fmt_at(TS *t, LONG cp);
int fmt_insertion(TS *t, LONG a, LONG b);         /* what text put in place of [a, b) takes */
int fmt_key(TS *t, int idx);                      /* its font: bold 1, italic 2 */
DWORD fmt_effects(TS *t, int idx);                /* its CFE_ bits, the default's under it */
COLORREF fmt_color(TS *t, int idx, COLORREF autocolor);
BOOL fmt_apply(TS *t, LONG a, LONG b, DWORD mask, DWORD effects, COLORREF color);
void fmt_query(TS *t, LONG a, LONG b, CHARFORMAT2W *out);   /* dwMask: what is the same over [a, b) */
BOOL fmt_toggle(TS *t, DWORD effect);             /* Ctrl+B, Ctrl+I, Ctrl+U */
HFONT fmt_font(TS *t, int key);
void fmt_free_fonts(TS *t);
void fmt_decorate(TS *t, DWORD effects, LONG x0, LONG x1, LONG y, void (*bar)(void *, LONG, LONG, LONG, LONG), void *ctx);
