/* tsint.h — the windowless Rich Edit's object, shared by textserv.c (the
 * text services) and tom.c (the Text Object Model over the same text) */
#pragma once
#include <novatextserv.h>

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);

#define UNDO_MAX    100
#define DEFAULT_MAX 32767                         /* Rich Edit's text limit until EM_EXLIMITTEXT */

typedef struct { WCHAR *text; LONG len, anchor, active; } SNAP;
typedef struct { LONG start, len, width; } LINE;  /* a laid-out line: [start, start+len) */

typedef struct TS {
    IUnknown inner;                               /* the object's own IUnknown (aggregation) */
    ITextServices2 iface;
    LONG ref;
    IUnknown *outer;
    ITextHost *host;                              /* not AddRef'd, as Windows' isn't */
    WCHAR *text;
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
void clear_undo(TS *t);
LONG splice(TS *t, LONG a, LONG b, const WCHAR *s, LONG n, BOOL undoable, BOOL typed);
LONG replace_sel(TS *t, const WCHAR *s, LONG n, BOOL undoable, BOOL typed);
void set_text(TS *t, const WCHAR *s, LONG n);
BOOL is_word(WCHAR c);
BOOL copy_range(TS *t, LONG a, LONG b);
BOOL can_paste(TS *t);
WCHAR *clipboard_text(TS *t);
HWND host_window(TS *t);

/* tom.c */
void tom_init(TS *t);
void tom_free(TS *t);
HRESULT tom_qi(TS *t, REFIID iid, void **out);
void tom_edited(TS *t, LONG a, LONG b, LONG k);   /* [a, b) became k characters */
void tom_clamp(TS *t);                            /* the text was replaced */
