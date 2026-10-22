/*
 * Microsoft.UI.Windowing: AppWindow over an existing Win32 window
 * (AppWindow.GetFromWindowId, which XAML's desktop window calls for its
 * window: one AppWindow per window, kept for the process).  Its id,
 * title, position, size and visibility are the window's (user32); its
 * presenter is an OverlappedPresenter whose state is the window's
 * (maximized, minimized, restored) and whose preferred minimum and maximum
 * sizes are kept; its title bar takes the colours and the extend-content
 * setting it is given (NovaOS draws the standard caption).  Changed is
 * raised as the window moves, sizes, shows or hides (the window is
 * subclassed).  AppWindow.Create (a window of its own) is not here yet.
 */
#include "lifted.h"

using namespace ixw;
namespace wg = ABI::Windows::Graphics;

typedef wf::ITypedEventHandler<AppWindow *, AppWindowChangedEventArgs *> ChangedHandler;
typedef wf::ITypedEventHandler<AppWindow *, AppWindowClosingEventArgs *> ClosingHandler;
typedef wf::ITypedEventHandler<AppWindow *, IInspectable *> DestroyingHandler;

struct ChangedArgs : Object, Stub<IAppWindowChangedEventArgs>, Stub<IAppWindowChangedEventArgs2>
{
    bool position, size, visibility, presenter;
    LIFTED_OBJECT(L"Microsoft.UI.Windowing.AppWindowChangedEventArgs")
    LIFTED_QI(ChangedArgs, IAppWindowChangedEventArgs, IAppWindowChangedEventArgs2)
    HRESULT STDMETHODCALLTYPE get_DidPositionChange(::boolean *v) override { *v = position; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_DidPresenterChange(::boolean *v) override { *v = presenter; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_DidSizeChange(::boolean *v) override { *v = size; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_DidVisibilityChange(::boolean *v) override { *v = visibility; return S_OK; }
};

struct Presenter : Object, Stub<IAppWindowPresenter>, Stub<IOverlappedPresenter>, Stub<IOverlappedPresenter2>,
                   Stub<IOverlappedPresenter3>
{
    HWND m_hwnd;
    wf::IReference<int> *m_min_w = nullptr, *m_min_h = nullptr, *m_max_w = nullptr, *m_max_h = nullptr;
    LIFTED_OBJECT(L"Microsoft.UI.Windowing.OverlappedPresenter")
    LIFTED_QI(Presenter, IAppWindowPresenter, IOverlappedPresenter, IOverlappedPresenter2, IOverlappedPresenter3)
    ~Presenter()
    {
        set_ref(m_min_w, (wf::IReference<int> *)nullptr);
        set_ref(m_min_h, (wf::IReference<int> *)nullptr);
        set_ref(m_max_w, (wf::IReference<int> *)nullptr);
        set_ref(m_max_h, (wf::IReference<int> *)nullptr);
    }
    LONG style() const { return GetWindowLongW(m_hwnd, GWL_STYLE); }
    void set_style(LONG bit, bool on)
    {
        LONG s = style();
        SetWindowLongW(m_hwnd, GWL_STYLE, on ? s | bit : s & ~bit);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    HRESULT STDMETHODCALLTYPE get_Kind(AppWindowPresenterKind *v) override { *v = AppWindowPresenterKind_Overlapped; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_HasBorder(::boolean *v) override { *v = (style() & (WS_BORDER | WS_THICKFRAME)) != 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_HasTitleBar(::boolean *v) override { *v = (style() & WS_CAPTION) == WS_CAPTION; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsAlwaysOnTop(::boolean *v) override
    {
        *v = (GetWindowLongW(m_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_IsAlwaysOnTop(::boolean v) override
    {
        SetWindowPos(m_hwnd, v ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsMaximizable(::boolean *v) override { *v = (style() & WS_MAXIMIZEBOX) != 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsMaximizable(::boolean v) override { set_style(WS_MAXIMIZEBOX, v); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsMinimizable(::boolean *v) override { *v = (style() & WS_MINIMIZEBOX) != 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsMinimizable(::boolean v) override { set_style(WS_MINIMIZEBOX, v); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsResizable(::boolean *v) override { *v = (style() & WS_THICKFRAME) != 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsResizable(::boolean v) override { set_style(WS_THICKFRAME, v); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_State(OverlappedPresenterState *v) override
    {
        *v = IsZoomed(m_hwnd) ? OverlappedPresenterState_Maximized
           : IsIconic(m_hwnd) ? OverlappedPresenterState_Minimized : OverlappedPresenterState_Restored;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Maximize() override { ShowWindow(m_hwnd, SW_MAXIMIZE); return S_OK; }
    HRESULT STDMETHODCALLTYPE Minimize() override { ShowWindow(m_hwnd, SW_MINIMIZE); return S_OK; }
    HRESULT STDMETHODCALLTYPE Restore() override { ShowWindow(m_hwnd, SW_RESTORE); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetBorderAndTitleBar(::boolean border, ::boolean title) override
    {
        LONG s = style() & ~(WS_CAPTION | WS_THICKFRAME);
        if (title) s |= WS_CAPTION;
        else if (border) s |= WS_BORDER;
        if (border && (style() & WS_THICKFRAME)) s |= WS_THICKFRAME;
        SetWindowLongW(m_hwnd, GWL_STYLE, s);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PreferredMinimumWidth(wf::IReference<int> **v) override { return copy_out(m_min_w, v); }
    HRESULT STDMETHODCALLTYPE put_PreferredMinimumWidth(wf::IReference<int> *v) override { set_ref(m_min_w, v); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_PreferredMinimumHeight(wf::IReference<int> **v) override { return copy_out(m_min_h, v); }
    HRESULT STDMETHODCALLTYPE put_PreferredMinimumHeight(wf::IReference<int> *v) override { set_ref(m_min_h, v); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_PreferredMaximumWidth(wf::IReference<int> **v) override { return copy_out(m_max_w, v); }
    HRESULT STDMETHODCALLTYPE put_PreferredMaximumWidth(wf::IReference<int> *v) override { set_ref(m_max_w, v); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_PreferredMaximumHeight(wf::IReference<int> **v) override { return copy_out(m_max_h, v); }
    HRESULT STDMETHODCALLTYPE put_PreferredMaximumHeight(wf::IReference<int> *v) override { set_ref(m_max_h, v); return S_OK; }
};

struct TitleBar : Object, Stub<IAppWindowTitleBar>, Stub<IAppWindowTitleBar2>, Stub<IAppWindowTitleBar3>
{
    ::boolean m_extends = false;
    LIFTED_OBJECT(L"Microsoft.UI.Windowing.AppWindowTitleBar")
    LIFTED_QI(TitleBar, IAppWindowTitleBar, IAppWindowTitleBar2, IAppWindowTitleBar3)
    HRESULT STDMETHODCALLTYPE get_ExtendsContentIntoTitleBar(::boolean *v) override { *v = m_extends; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_ExtendsContentIntoTitleBar(::boolean v) override { m_extends = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Height(INT32 *v) override { *v = GetSystemMetrics(SM_CYCAPTION); return S_OK; }
};

struct AppWindowObj : Object, Stub<IAppWindow>, Stub<IAppWindow2>, Stub<IAppWindow3>, Stub<IAppWindow4>
{
    AppWindowObj *m_next;
    HWND m_hwnd;
    WNDPROC m_old;
    Presenter *m_presenter;
    TitleBar *m_titlebar;
    Event<ChangedHandler> m_changed;
    Event<ClosingHandler> m_closing;
    Event<DestroyingHandler> m_destroying;
    LIFTED_OBJECT(L"Microsoft.UI.Windowing.AppWindow")
    LIFTED_QI(AppWindowObj, IAppWindow, IAppWindow2, IAppWindow3, IAppWindow4)
    ~AppWindowObj()
    {
        if (m_presenter) m_presenter->Release();
        if (m_titlebar) m_titlebar->Release();
    }

    void changed(bool position, bool size, bool visibility)
    {
        if (m_changed.Empty()) return;
        ChangedArgs *a = new ChangedArgs();
        if (!a) return;
        a->position = position;
        a->size = size;
        a->visibility = visibility;
        AddRef();
        m_changed.Raise(static_cast<IAppWindow *>(this), static_cast<IAppWindowChangedEventArgs *>(a));
        Release();
        a->Release();
    }
    static LRESULT CALLBACK subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HRESULT STDMETHODCALLTYPE get_Id(mui::WindowId *v) override { v->Value = (UINT64)(ULONG_PTR)m_hwnd; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsShownInSwitchers(::boolean *v) override
    {
        *v = !(GetWindowLongW(m_hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsVisible(::boolean *v) override { *v = IsWindowVisible(m_hwnd) != 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_OwnerWindowId(mui::WindowId *v) override
    {
        v->Value = (UINT64)(ULONG_PTR)GetWindow(m_hwnd, GW_OWNER);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Position(wg::PointInt32 *v) override
    {
        RECT r;
        GetWindowRect(m_hwnd, &r);
        v->X = r.left;
        v->Y = r.top;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Size(wg::SizeInt32 *v) override
    {
        RECT r;
        GetWindowRect(m_hwnd, &r);
        v->Width = r.right - r.left;
        v->Height = r.bottom - r.top;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ClientSize(wg::SizeInt32 *v) override
    {
        RECT r;
        GetClientRect(m_hwnd, &r);
        v->Width = r.right;
        v->Height = r.bottom;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ResizeClient(wg::SizeInt32 size) override
    {
        RECT r = { 0, 0, size.Width, size.Height };
        AdjustWindowRectEx(&r, GetWindowLongW(m_hwnd, GWL_STYLE), GetMenu(m_hwnd) != nullptr, GetWindowLongW(m_hwnd, GWL_EXSTYLE));
        SetWindowPos(m_hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Presenter(IAppWindowPresenter **v) override { return copy_out<IAppWindowPresenter>(m_presenter, v); }
    HRESULT STDMETHODCALLTYPE get_Title(HSTRING *v) override
    {
        WCHAR buf[512];
        int n = GetWindowTextW(m_hwnd, buf, 512);
        return WindowsCreateString(buf, (UINT32)n, v);
    }
    HRESULT STDMETHODCALLTYPE put_Title(HSTRING v) override
    {
        SetWindowTextW(m_hwnd, WindowsGetStringRawBuffer(v, nullptr));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_TitleBar(IAppWindowTitleBar **v) override { return copy_out<IAppWindowTitleBar>(m_titlebar, v); }
    HRESULT STDMETHODCALLTYPE Destroy() override { DestroyWindow(m_hwnd); return S_OK; }
    HRESULT STDMETHODCALLTYPE Hide() override { ShowWindow(m_hwnd, SW_HIDE); return S_OK; }
    HRESULT STDMETHODCALLTYPE Show() override { ShowWindow(m_hwnd, SW_SHOW); return S_OK; }
    HRESULT STDMETHODCALLTYPE ShowWithActivation(::boolean activate) override
    {
        ShowWindow(m_hwnd, activate ? SW_SHOW : SW_SHOWNA);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Move(wg::PointInt32 p) override
    {
        SetWindowPos(m_hwnd, nullptr, p.X, p.Y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Resize(wg::SizeInt32 s) override
    {
        SetWindowPos(m_hwnd, nullptr, 0, 0, s.Width, s.Height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveAndResize(wg::RectInt32 r) override
    {
        SetWindowPos(m_hwnd, nullptr, r.X, r.Y, r.Width, r.Height, SWP_NOZORDER | SWP_NOACTIVATE);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPresenterByKind(AppWindowPresenterKind kind) override
    {
        return kind == AppWindowPresenterKind_Overlapped || kind == AppWindowPresenterKind_Default ? S_OK : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE add_Changed(ChangedHandler *h, EventRegistrationToken *t) override { return m_changed.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_Changed(EventRegistrationToken t) override { return m_changed.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_Closing(ClosingHandler *h, EventRegistrationToken *t) override { return m_closing.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_Closing(EventRegistrationToken t) override { return m_closing.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_Destroying(DestroyingHandler *h, EventRegistrationToken *t) override { return m_destroying.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_Destroying(EventRegistrationToken t) override { return m_destroying.Remove(t); }
};

static SRWLOCK g_windows_lock = SRWLOCK_INIT;
static AppWindowObj *g_windows;

static AppWindowObj *find(HWND hwnd)
{
    AcquireSRWLockShared(&g_windows_lock);
    AppWindowObj *a = g_windows;
    while (a && a->m_hwnd != hwnd) a = a->m_next;
    ReleaseSRWLockShared(&g_windows_lock);
    return a;
}

LRESULT CALLBACK AppWindowObj::subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    AppWindowObj *a = find(hwnd);
    if (!a) return DefWindowProcW(hwnd, msg, wp, lp);
    WNDPROC old = a->m_old;
    LRESULT r = CallWindowProcW(old, hwnd, msg, wp, lp);
    switch (msg) {
    case WM_WINDOWPOSCHANGED: {
        const WINDOWPOS *p = (const WINDOWPOS *)lp;
        bool moved = !(p->flags & SWP_NOMOVE), sized = !(p->flags & SWP_NOSIZE);
        bool shown = (p->flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW)) != 0;
        if (moved || sized || shown) a->changed(moved, sized, shown);
        break;
    }
    case WM_NCDESTROY:
        a->m_destroying.Raise(static_cast<IAppWindow *>(a), (IInspectable *)nullptr);
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)old);
        AcquireSRWLockExclusive(&g_windows_lock);
        for (AppWindowObj **pp = &g_windows; *pp; pp = &(*pp)->m_next)
            if (*pp == a) { *pp = a->m_next; break; }
        ReleaseSRWLockExclusive(&g_windows_lock);
        a->Release();
        break;
    }
    return r;
}

struct AppWindowStatics : Object, Stub<IAppWindowStatics>, IActivationFactory
{
    LIFTED_OBJECT(L"Microsoft.UI.Windowing.AppWindow")
    LIFTED_QI(AppWindowStatics, IAppWindowStatics, IActivationFactory)
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable **out) override { *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFromWindowId(mui::WindowId id, IAppWindow **result) override
    {
        *result = nullptr;
        HWND hwnd = (HWND)(ULONG_PTR)id.Value;
        if (!IsWindow(hwnd)) return E_INVALIDARG;
        AppWindowObj *a = find(hwnd);
        if (!a) {
            a = new AppWindowObj();
            if (!a) return E_OUTOFMEMORY;
            a->m_hwnd = hwnd;
            a->m_presenter = new Presenter();
            a->m_titlebar = new TitleBar();
            if (!a->m_presenter || !a->m_titlebar) {
                a->Release();
                return E_OUTOFMEMORY;
            }
            a->m_presenter->m_hwnd = hwnd;
            AcquireSRWLockExclusive(&g_windows_lock);
            a->m_next = g_windows;
            g_windows = a;
            ReleaseSRWLockExclusive(&g_windows_lock);
            /* (the list's reference, dropped as the window goes) */
            a->m_old = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)AppWindowObj::subclass);
        }
        a->AddRef();
        *result = a;
        return S_OK;
    }
};

IActivationFactory *app_window_factory() { return new AppWindowStatics(); }
