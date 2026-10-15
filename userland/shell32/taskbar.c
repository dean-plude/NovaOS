/*
 * taskbar.c — the TaskbarList class (CLSID_TaskbarList: ITaskbarList,
 * ITaskbarList2, ITaskbarList3, ITaskbarList4), which programs use to put
 * a progress bar, an overlay icon or thumbnail buttons on their taskbar
 * button.  NovaOS's taskbar shows none of these yet, so every call
 * succeeds and changes nothing; what matters is that the class is there
 * (ShareX shows an error box after each capture without it).  On Windows
 * the class lives in ExplorerFrame.dll.
 */

#define NOVA_BUILD_SHELL32
#include <windows.h>
#include <objbase.h>

#define S_OK_          ((HRESULT)0)
#define E_INVALIDARG_  ((HRESULT)0x80070057L)
#define E_NOINTERFACE_ ((HRESULT)0x80004002L)
#define E_OUTOFMEMORY_ ((HRESULT)0x8007000EL)
#define CLASS_E_NOAGGREGATION_ ((HRESULT)0x80040110L)

static const GUID IID_IUnknown_      = { 0x00000000, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_ICF_           = { 0x00000001, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_ITaskbarList_  = { 0x56FDF342, 0xFD6D, 0x11D0, { 0x95, 0x8A, 0x00, 0x60, 0x97, 0xC9, 0xA0, 0x90 } };
static const GUID IID_ITaskbarList2_ = { 0x602D4995, 0xB13A, 0x429B, { 0xA6, 0x6E, 0x19, 0x35, 0xE4, 0x4F, 0x43, 0x17 } };
static const GUID IID_ITaskbarList3_ = { 0xEA1AFB91, 0x9E28, 0x4B86, { 0x90, 0xE9, 0x9E, 0x9F, 0x8A, 0x5E, 0xEF, 0xAF } };
static const GUID IID_ITaskbarList4_ = { 0xC43DC798, 0x95D1, 0x4BEA, { 0x90, 0x30, 0xBB, 0x99, 0xE2, 0x98, 0x3A, 0x1A } };

static int same_guid(const GUID *a, const GUID *b)
{
    const BYTE *x = (const BYTE *)a, *y = (const BYTE *)b;
    for (int i = 0; i < 16; i++) if (x[i] != y[i]) return 0;
    return 1;
}

typedef struct { void *vtbl; LONG refs; } TaskbarList;

static HRESULT STDMETHODCALLTYPE tb_qi(TaskbarList *t, REFIID riid, void **ppv)
{
    if (!ppv) return E_INVALIDARG_;
    if (same_guid(riid, &IID_IUnknown_) || same_guid(riid, &IID_ITaskbarList_) || same_guid(riid, &IID_ITaskbarList2_) ||
        same_guid(riid, &IID_ITaskbarList3_) || same_guid(riid, &IID_ITaskbarList4_)) {
        InterlockedIncrement(&t->refs);
        *ppv = t;
        return S_OK_;
    }
    *ppv = 0;
    return E_NOINTERFACE_;
}
static ULONG STDMETHODCALLTYPE tb_addref(TaskbarList *t) { return (ULONG)InterlockedIncrement(&t->refs); }
static ULONG STDMETHODCALLTYPE tb_release(TaskbarList *t)
{
    LONG n = InterlockedDecrement(&t->refs);
    if (!n) HeapFree(GetProcessHeap(), 0, t);
    return (ULONG)n;
}
/* every other method: S_OK, whatever its arguments (all take the object
 * and at most five more, in registers or above the shadow space) */
static HRESULT STDMETHODCALLTYPE tb_ok(TaskbarList *t) { (void)t; return S_OK_; }

static void *const g_tb_vtbl[] = {
    tb_qi, tb_addref, tb_release,
    tb_ok, tb_ok, tb_ok, tb_ok, tb_ok,                  /* HrInit, AddTab, DeleteTab, ActivateTab, SetActiveAlt */
    tb_ok,                                              /* MarkFullscreenWindow */
    tb_ok, tb_ok, tb_ok, tb_ok, tb_ok, tb_ok,           /* SetProgressValue/State, RegisterTab, UnregisterTab, SetTabOrder, SetTabActive */
    tb_ok, tb_ok, tb_ok, tb_ok, tb_ok, tb_ok,           /* ThumbBar*, SetOverlayIcon, SetThumbnailTooltip, SetThumbnailClip */
    tb_ok,                                              /* SetTabProperties */
};

static HRESULT STDMETHODCALLTYPE cf_qi(void *t, REFIID riid, void **ppv)
{
    if (!ppv) return E_INVALIDARG_;
    if (same_guid(riid, &IID_IUnknown_) || same_guid(riid, &IID_ICF_)) { *ppv = t; return S_OK_; }
    *ppv = 0;
    return E_NOINTERFACE_;
}
static ULONG STDMETHODCALLTYPE cf_addref(void *t)  { (void)t; return 2; }
static ULONG STDMETHODCALLTYPE cf_release(void *t) { (void)t; return 1; }
static HRESULT STDMETHODCALLTYPE cf_create(void *t, IUnknown *outer, REFIID riid, void **ppv)
{
    (void)t;
    if (!ppv) return E_INVALIDARG_;
    *ppv = 0;
    if (outer) return CLASS_E_NOAGGREGATION_;
    TaskbarList *tb = HeapAlloc(GetProcessHeap(), 0, sizeof(*tb));
    if (!tb) return E_OUTOFMEMORY_;
    tb->vtbl = (void *)g_tb_vtbl;
    tb->refs = 1;
    HRESULT hr = tb_qi(tb, riid, ppv);
    tb_release(tb);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cf_lock(void *t, BOOL lock) { (void)t; (void)lock; return S_OK_; }
static void *const g_cf_vtbl[] = { cf_qi, cf_addref, cf_release, cf_create, cf_lock };
static struct { void *vtbl; } g_tb_factory = { (void *)g_cf_vtbl };

HRESULT taskbar_class_object(REFIID riid, void **ppv) { return cf_qi(&g_tb_factory, riid, ppv); }
