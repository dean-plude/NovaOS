/*
 * folder.c — the shell namespace's folders (IShellFolder) over the file
 * system: SHGetDesktopFolder's desktop, whose children are the drives, and
 * a folder for each directory, bound from an item ID list.
 *
 * An item here is one SHITEMID holding a full path (UTF-16), as everywhere
 * in NovaOS's shell32, so a child's relative ID list is also absolute and
 * a parent's list followed by a child's names the child (its last item).
 * Display names are the file names (a drive: "Local Disk (C:)"), or the
 * full path for SHGDN_FORPARSING.  Views, context menus and renaming are
 * not offered (E_NOTIMPL), as for a folder without them.
 */
#define NOVA_BUILD_SHELL32
#include <windows.h>

#define E_NOTIMPL_     ((HRESULT)0x80004001L)
#define E_NOINTERFACE_ ((HRESULT)0x80004002L)
#define E_POINTER_     ((HRESULT)0x80004003L)
#define E_INVALIDARG_  ((HRESULT)0x80070057L)
#define E_OUTOFMEMORY_ ((HRESULT)0x8007000EL)
#define HR_WIN32(e)    ((HRESULT)(0x80070000L | (e)))

/* IShellFolder flags (shobjidl.h) */
#define SHCONTF_FOLDERS          0x20
#define SHCONTF_NONFOLDERS       0x40
#define SHCONTF_INCLUDEHIDDEN    0x80
#define SHGDN_INFOLDER           0x1
#define SHGDN_FORPARSING         0x8000
#define SFGAO_CANCOPY            0x1
#define SFGAO_CANMOVE            0x2
#define SFGAO_CANRENAME          0x10
#define SFGAO_CANDELETE          0x20
#define SFGAO_READONLY           0x40000
#define SFGAO_HIDDEN             0x80000
#define SFGAO_STREAM             0x400000
#define SFGAO_STORAGEANCESTOR    0x800000
#define SFGAO_FILESYSANCESTOR    0x10000000
#define SFGAO_FOLDER             0x20000000
#define SFGAO_FILESYSTEM         0x40000000
#define SFGAO_HASSUBFOLDER       0x80000000u
#define STRRET_WSTR              0

typedef struct { WORD cb; BYTE abID[1]; } ItemId;
typedef struct { UINT uType; union { LPWSTR pOleStr; UINT uOffset; char cStr[MAX_PATH]; }; } StrRet;

LPVOID sh_pidl_from_path(const WCHAR *path);            /* shell32.c */

static const GUID IID_IUnknown_ = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IShellFolder_ = { 0x000214E6, 0x0000, 0x0000, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IEnumIDList_ = { 0x000214F2, 0x0000, 0x0000, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };

static int glen(const WCHAR *s) { int n = 0; while (s[n]) n++; return n; }
static void wput(WCHAR *d, const WCHAR *s, int n) { for (int i = 0; i < n; i++) d[i] = s[i]; }   /* (shell32 has no C runtime) */
static BOOL same_guid(REFIID a, const GUID *b)
{
    const BYTE *x = (const BYTE *)a, *y = (const BYTE *)b;
    for (int i = 0; i < (int)sizeof(GUID); i++) if (x[i] != y[i]) return FALSE;
    return TRUE;
}

/* The path an ID list names (its last item; "" for the desktop) */
static void pidl_path(const void *pidl, WCHAR *out)
{
    out[0] = 0;
    const BYTE *p = pidl, *last = NULL;
    for (; p && *(const WORD *)p; p += *(const WORD *)p) last = p;
    if (!last) return;
    const WCHAR *s = (const WCHAR *)(last + 2);
    int n = 0, cap = (*(const WORD *)last - 2) / 2;
    while (n < cap && n < MAX_PATH - 1 && s[n]) { out[n] = s[n]; n++; }
    out[n] = 0;
}

/* ---- the item enumerator ----------------------------------------------- */
typedef struct EnumIds { const struct EnumVtbl *v; LONG refs; WCHAR **paths; ULONG n, at; } EnumIds;
struct EnumVtbl {
    HRESULT (WINAPI *qi)(EnumIds *, REFIID, void **);
    ULONG (WINAPI *add)(EnumIds *);
    ULONG (WINAPI *rel)(EnumIds *);
    HRESULT (WINAPI *next)(EnumIds *, ULONG, void **, ULONG *);
    HRESULT (WINAPI *skip)(EnumIds *, ULONG);
    HRESULT (WINAPI *reset)(EnumIds *);
    HRESULT (WINAPI *clone)(EnumIds *, EnumIds **);
};
static const struct EnumVtbl g_enum_vtbl;

static HRESULT WINAPI en_qi(EnumIds *e, REFIID iid, void **out)
{
    if (!out) return E_POINTER_;
    if (same_guid(iid, &IID_IUnknown_) || same_guid(iid, &IID_IEnumIDList_)) { *out = e; e->v->add(e); return S_OK; }
    *out = NULL;
    return E_NOINTERFACE_;
}
static ULONG WINAPI en_add(EnumIds *e) { return (ULONG)InterlockedIncrement(&e->refs); }
static ULONG WINAPI en_rel(EnumIds *e)
{
    LONG r = InterlockedDecrement(&e->refs);
    if (!r) {
        for (ULONG i = 0; i < e->n; i++) HeapFree(GetProcessHeap(), 0, e->paths[i]);
        HeapFree(GetProcessHeap(), 0, e->paths);
        HeapFree(GetProcessHeap(), 0, e);
    }
    return (ULONG)r;
}
static HRESULT WINAPI en_next(EnumIds *e, ULONG want, void **out, ULONG *got)
{
    ULONG k = 0;
    if (!out || (want > 1 && !got)) return E_POINTER_;
    while (k < want && e->at < e->n) {
        if (!(out[k] = sh_pidl_from_path(e->paths[e->at]))) break;
        k++; e->at++;
    }
    if (got) *got = k;
    return k == want ? S_OK : S_FALSE;
}
static HRESULT WINAPI en_skip(EnumIds *e, ULONG n) { e->at = e->at + n > e->n ? e->n : e->at + n; return e->at < e->n ? S_OK : S_FALSE; }
static HRESULT WINAPI en_reset(EnumIds *e) { e->at = 0; return S_OK; }
static HRESULT WINAPI en_clone(EnumIds *e, EnumIds **out) { (void)e; if (out) *out = NULL; return E_NOTIMPL_; }
static const struct EnumVtbl g_enum_vtbl = { en_qi, en_add, en_rel, en_next, en_skip, en_reset, en_clone };

static BOOL en_push(EnumIds *e, const WCHAR *path, ULONG *cap)
{
    if (e->n == *cap) {
        ULONG nc = *cap ? *cap * 2 : 16;
        WCHAR **p = e->paths ? HeapReAlloc(GetProcessHeap(), 0, e->paths, nc * sizeof(*p)) : HeapAlloc(GetProcessHeap(), 0, nc * sizeof(*p));
        if (!p) return FALSE;
        e->paths = p;
        *cap = nc;
    }
    int n = glen(path);
    WCHAR *c = HeapAlloc(GetProcessHeap(), 0, 2 * ((SIZE_T)n + 1));
    if (!c) return FALSE;
    wput(c, path, n + 1);
    e->paths[e->n++] = c;
    return TRUE;
}

/* ---- folders ----------------------------------------------------------- */
typedef struct Folder { const struct FolderVtbl *v; LONG refs; WCHAR path[MAX_PATH]; } Folder;   /* path "": the desktop */
struct FolderVtbl {
    HRESULT (WINAPI *qi)(Folder *, REFIID, void **);
    ULONG (WINAPI *add)(Folder *);
    ULONG (WINAPI *rel)(Folder *);
    HRESULT (WINAPI *parse)(Folder *, HWND, void *, LPWSTR, ULONG *, void **, ULONG *);
    HRESULT (WINAPI *enumobj)(Folder *, HWND, DWORD, EnumIds **);
    HRESULT (WINAPI *bind)(Folder *, const void *, void *, REFIID, void **);
    HRESULT (WINAPI *bindstg)(Folder *, const void *, void *, REFIID, void **);
    HRESULT (WINAPI *compare)(Folder *, LPARAM, const void *, const void *);
    HRESULT (WINAPI *view)(Folder *, HWND, REFIID, void **);
    HRESULT (WINAPI *attrs)(Folder *, UINT, const void **, ULONG *);
    HRESULT (WINAPI *uiobj)(Folder *, HWND, UINT, const void **, REFIID, UINT *, void **);
    HRESULT (WINAPI *name)(Folder *, const void *, DWORD, StrRet *);
    HRESULT (WINAPI *setname)(Folder *, HWND, const void *, LPCWSTR, DWORD, void **);
};
static const struct FolderVtbl g_folder_vtbl;

static Folder *folder_new(const WCHAR *path)
{
    Folder *f = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*f));
    if (!f) return NULL;
    f->v = &g_folder_vtbl;
    f->refs = 1;
    int n = glen(path);
    if (n >= MAX_PATH) n = MAX_PATH - 1;
    wput(f->path, path, n);
    f->path[n] = 0;
    return f;
}

static HRESULT WINAPI f_qi(Folder *f, REFIID iid, void **out)
{
    if (!out) return E_POINTER_;
    if (same_guid(iid, &IID_IUnknown_) || same_guid(iid, &IID_IShellFolder_)) { *out = f; f->v->add(f); return S_OK; }
    *out = NULL;
    return E_NOINTERFACE_;
}
static ULONG WINAPI f_add(Folder *f) { return (ULONG)InterlockedIncrement(&f->refs); }
static ULONG WINAPI f_rel(Folder *f)
{
    LONG r = InterlockedDecrement(&f->refs);
    if (!r) HeapFree(GetProcessHeap(), 0, f);
    return (ULONG)r;
}

/* What a file or folder is, as SFGAO_* flags (0: it does not exist) */
static ULONG item_attrs(const WCHAR *path)
{
    DWORD a = GetFileAttributesW(path);
    if (a == INVALID_FILE_ATTRIBUTES) return 0;
    ULONG r = SFGAO_FILESYSTEM | SFGAO_CANCOPY | SFGAO_CANMOVE | SFGAO_CANRENAME | SFGAO_CANDELETE;
    if (a & FILE_ATTRIBUTE_DIRECTORY) r |= SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_FILESYSANCESTOR | SFGAO_STORAGEANCESTOR;
    else r |= SFGAO_STREAM;
    if (a & FILE_ATTRIBUTE_READONLY) r |= SFGAO_READONLY;
    if (a & FILE_ATTRIBUTE_HIDDEN) r |= SFGAO_HIDDEN;
    return r;
}

/* A name inside this folder (or a full path) as an ID list */
static HRESULT WINAPI f_parse(Folder *f, HWND w, void *bc, LPWSTR name, ULONG *eaten, void **out, ULONG *attrs)
{
    (void)w; (void)bc;
    if (!out) return E_POINTER_;
    *out = NULL;
    if (!name) return E_INVALIDARG_;
    WCHAR rel[MAX_PATH], full[MAX_PATH];
    int n = glen(name), k = 0;
    BOOL absolute = n >= 2 && (name[1] == ':' || (name[0] == '\\' && name[1] == '\\'));
    if (!absolute && f->path[0]) {
        int pl = glen(f->path);
        if (pl + 1 + n >= MAX_PATH) return E_INVALIDARG_;
        wput(rel, f->path, pl);
        k = pl;
        if (rel[k - 1] != '\\') rel[k++] = '\\';
    }
    if (k + n >= MAX_PATH) return E_INVALIDARG_;
    wput(rel + k, name, n + 1);
    if (!GetFullPathNameW(rel, MAX_PATH, full, NULL)) return HR_WIN32(GetLastError());
    ULONG a = item_attrs(full);
    if (!a) return HR_WIN32(ERROR_FILE_NOT_FOUND);
    if (!(*out = sh_pidl_from_path(full))) return E_OUTOFMEMORY_;
    if (eaten) *eaten = (ULONG)n;
    if (attrs) *attrs &= a;
    return S_OK;
}

static HRESULT WINAPI f_enum(Folder *f, HWND w, DWORD flags, EnumIds **out)
{
    (void)w;
    if (!out) return E_POINTER_;
    *out = NULL;
    EnumIds *e = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (!e) return E_OUTOFMEMORY_;
    e->v = &g_enum_vtbl;
    e->refs = 1;
    ULONG cap = 0;
    if (!f->path[0]) {                                   /* the desktop: the drives */
        DWORD drives = (flags & SHCONTF_FOLDERS) ? GetLogicalDrives() : 0;
        for (int d = 0; d < 26; d++)
            if (drives & (1u << d)) {
                WCHAR root[4] = { (WCHAR)('A' + d), ':', '\\', 0 };
                en_push(e, root, &cap);
            }
    } else {
        WCHAR pat[MAX_PATH + 2];
        int pl = glen(f->path);
        if (pl + 3 >= MAX_PATH) { en_rel(e); return E_INVALIDARG_; }
        wput(pat, f->path, pl);
        if (pat[pl - 1] != '\\') pat[pl++] = '\\';
        pat[pl] = '*'; pat[pl + 1] = 0;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.cFileName[0] == '.' && (!fd.cFileName[1] || (fd.cFileName[1] == '.' && !fd.cFileName[2]))) continue;
                BOOL dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (!(flags & (dir ? SHCONTF_FOLDERS : SHCONTF_NONFOLDERS))) continue;
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) && !(flags & SHCONTF_INCLUDEHIDDEN)) continue;
                int nl = glen(fd.cFileName);
                if (pl + nl >= MAX_PATH) continue;
                wput(pat + pl, fd.cFileName, nl + 1);
                en_push(e, pat, &cap);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    *out = e;
    return S_OK;
}

static HRESULT WINAPI f_bind(Folder *f, const void *pidl, void *bc, REFIID iid, void **out)
{
    (void)f; (void)bc;
    if (!out) return E_POINTER_;
    *out = NULL;
    WCHAR p[MAX_PATH];
    pidl_path(pidl, p);
    if (!p[0]) return E_INVALIDARG_;
    if (!(item_attrs(p) & SFGAO_FOLDER)) return HR_WIN32(ERROR_DIRECTORY);
    Folder *c = folder_new(p);
    if (!c) return E_OUTOFMEMORY_;
    HRESULT hr = c->v->qi(c, iid, out);
    c->v->rel(c);
    return hr;
}

static HRESULT WINAPI f_bindstg(Folder *f, const void *pidl, void *bc, REFIID iid, void **out)
{
    (void)f; (void)pidl; (void)bc; (void)iid;
    if (out) *out = NULL;
    return E_NOTIMPL_;
}

/* Folders first, then by name, as Explorer sorts (the HRESULT's code is
 * negative, 0 or positive) */
static HRESULT WINAPI f_compare(Folder *f, LPARAM how, const void *a, const void *b)
{
    (void)f; (void)how;
    WCHAR pa[MAX_PATH], pb[MAX_PATH];
    pidl_path(a, pa);
    pidl_path(b, pb);
    int da = (item_attrs(pa) & SFGAO_FOLDER) != 0, db = (item_attrs(pb) & SFGAO_FOLDER) != 0;
    int r = da != db ? db - da : CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, pa, -1, pb, -1) - CSTR_EQUAL;
    return (HRESULT)(USHORT)(SHORT)(r < 0 ? -1 : r > 0);
}

static HRESULT WINAPI f_view(Folder *f, HWND w, REFIID iid, void **out)
{
    (void)f; (void)w; (void)iid;
    if (out) *out = NULL;
    return E_NOTIMPL_;
}

static HRESULT WINAPI f_attrs(Folder *f, UINT n, const void **items, ULONG *inout)
{
    (void)f;
    if (!inout) return E_POINTER_;
    ULONG all = ~0u;
    for (UINT i = 0; i < n; i++) {
        WCHAR p[MAX_PATH];
        pidl_path(items ? items[i] : NULL, p);
        all &= item_attrs(p);
    }
    *inout &= n ? all : SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_FILESYSANCESTOR;
    return S_OK;
}

static HRESULT WINAPI f_uiobj(Folder *f, HWND w, UINT n, const void **items, REFIID iid, UINT *res, void **out)
{
    (void)f; (void)w; (void)n; (void)items; (void)iid; (void)res;
    if (out) *out = NULL;
    return E_NOTIMPL_;
}

static HRESULT WINAPI f_name(Folder *f, const void *pidl, DWORD flags, StrRet *out)
{
    (void)f;
    if (!out) return E_POINTER_;
    WCHAR p[MAX_PATH], shown[MAX_PATH + 32];
    pidl_path(pidl, p);
    const WCHAR *s = p;
    if (!(flags & SHGDN_FORPARSING)) {
        int n = glen(p);
        if (n == 3 && p[1] == ':' && p[2] == '\\') {      /* a drive: its label, as Explorer names it */
            WCHAR label[MAX_PATH];
            label[0] = 0;
            GetVolumeInformationW(p, label, MAX_PATH, NULL, NULL, NULL, NULL, 0);
            const WCHAR *l = label[0] ? label : L"Local Disk";
            int k = 0;
            while (*l && k < MAX_PATH) shown[k++] = *l++;
            shown[k++] = ' '; shown[k++] = '('; shown[k++] = p[0]; shown[k++] = ':'; shown[k++] = ')'; shown[k] = 0;
            s = shown;
        } else {
            while (n > 0 && p[n - 1] != '\\') n--;
            s = p + n;
        }
    }
    int n = glen(s);
    WCHAR *c = HeapAlloc(GetProcessHeap(), 0, 2 * ((SIZE_T)n + 1));        /* (CoTaskMemFree frees it: the same heap) */
    if (!c) return E_OUTOFMEMORY_;
    wput(c, s, n + 1);
    out->uType = STRRET_WSTR;
    out->pOleStr = c;
    return S_OK;
}

static HRESULT WINAPI f_setname(Folder *f, HWND w, const void *pidl, LPCWSTR name, DWORD flags, void **out)
{
    (void)f; (void)w; (void)pidl; (void)name; (void)flags;
    if (out) *out = NULL;
    return E_NOTIMPL_;
}

static const struct FolderVtbl g_folder_vtbl = {
    f_qi, f_add, f_rel, f_parse, f_enum, f_bind, f_bindstg, f_compare, f_view, f_attrs, f_uiobj, f_name, f_setname
};

SHSTDAPI_(HRESULT) SHGetDesktopFolder(void **out)
{
    if (!out) return E_POINTER_;
    *out = folder_new(L"");
    return *out ? S_OK : E_OUTOFMEMORY_;
}

/* The folder holding an ID list's item, and that item (within the list):
 * the item's directory, or the desktop for a drive */
SHSTDAPI_(HRESULT) SHBindToParent(const void *pidl, REFIID iid, void **out, const void **last)
{
    if (out) *out = NULL;
    if (last) *last = NULL;
    if (!pidl || !out) return E_INVALIDARG_;
    const BYTE *p = pidl, *item = NULL;
    for (; *(const WORD *)p; p += *(const WORD *)p) item = p;
    WCHAR path[MAX_PATH];
    pidl_path(pidl, path);
    if (!item || !path[0]) return E_INVALIDARG_;
    int n = glen(path);
    if (n > 3 && path[n - 1] == '\\') n--;
    while (n > 0 && path[n - 1] != '\\') n--;
    if (n > 3) n--;                                      /* "C:\dir\x" -> "C:\dir"; "C:\x" -> "C:\" */
    path[n == 3 && path[1] == ':' && glen(path) == 3 ? 0 : n] = 0;
    Folder *f = folder_new(path);
    if (!f) return E_OUTOFMEMORY_;
    HRESULT hr = f->v->qi(f, iid, out);
    f->v->rel(f);
    if (SUCCEEDED(hr) && last) *last = item;
    return hr;
}
