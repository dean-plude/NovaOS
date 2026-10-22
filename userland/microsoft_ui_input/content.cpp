/*
 * Microsoft.UI.Content: content islands and the desktop site bridge that
 * shows one in a Win32 window.
 *
 * Island.Create(visual) makes an island over a composition visual
 * tree (NovaOS's compositor, dcomp.dll); the island has an environment
 * (ContentIslandEnvironment: its top-level window and display), input
 * sources (input.cpp) and, once a bridge connects it, a size, a scale of
 * 1.0 and a site that is visible and enabled.
 *
 * SiteBridge.Create(compositor, parent) makes a child window
 * of the parent window (hidden until shown or moved with SWP_SHOWWINDOW);
 * Connect(island) shows the island's visual tree in it through a
 * composition target on the window (the compositor's
 * ICompositorDesktopInterop), and from then on the window's size is the
 * island's ActualSize, raised as StateChanged, and its input messages are
 * the island's input sources' events.  An island's root can be replaced
 * (IContentIslandExperimental::put_Root, which XAML's composition tree
 * host calls); the target shows the new root.
 */
#include "lifted.h"

using namespace ixp;

/* (windows.ui.composition.interop.h) */
struct __declspec(uuid("29E691FA-4567-4DCA-B319-D0F207EB6807")) ICompositorDesktopInterop : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE CreateDesktopWindowTarget(HWND hwnd, BOOL isTopmost, IInspectable **result) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnsureOnThread(DWORD threadId) = 0;
};

static SRWLOCK g_islands_lock = SRWLOCK_INIT;
static Island *g_islands;        /* every island, newest first */
static UINT64 g_next_id = 1;

typedef wf::ITypedEventHandler<ixp::ContentIsland *, ContentIslandStateChangedEventArgs *> IslandStateHandler;
typedef wf::ITypedEventHandler<ixp::ContentIsland *, ContentIslandAutomationProviderRequestedEventArgs *> IslandAutomationHandler;
typedef wf::ITypedEventHandler<ContentIslandEnvironment *, ContentEnvironmentStateChangedEventArgs *> EnvStateHandler;
typedef wf::ITypedEventHandler<ContentIslandEnvironment *, ContentEnvironmentSettingChangedEventArgs *> EnvSettingHandler;
typedef wf::ITypedEventHandler<ContentIslandEnvironment *, IInspectable *> EnvThemeHandler;

/* ---- ContentIslandStateChangedEventArgs --------------------------------- */
struct IslandStateArgs : Object, Stub<IContentIslandStateChangedEventArgs>, Stub<IContentIslandStateChangedEventArgs2>
{
    bool size, visible, enabled, direction, scale;
    LIFTED_OBJECT(L"Microsoft.UI.Content.ContentIslandStateChangedEventArgs")
    LIFTED_QI(IslandStateArgs, IContentIslandStateChangedEventArgs, IContentIslandStateChangedEventArgs2)
    HRESULT STDMETHODCALLTYPE get_DidActualSizeChange(::boolean *v) override { *v = size; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_DidSiteEnabledChange(::boolean *v) override { *v = enabled; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_DidSiteVisibleChange(::boolean *v) override { *v = visible; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_DidLayoutDirectionChange(::boolean *v) override { *v = direction; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_DidRasterizationScaleChange(::boolean *v) override { *v = scale; return S_OK; }
};

/* ---- ContentIslandEnvironment -------------------------------------------- */
struct IslandEnvironment : Object, Stub<IContentIslandEnvironment>, Stub<IContentIslandEnvironment2>,
                           Stub<IContentIslandEnvironmentExperimental>
{
    Island *m_island;            /* (not a reference: the island owns this) */
    Event<EnvStateHandler> m_state;
    Event<EnvSettingHandler> m_setting;
    Event<EnvThemeHandler> m_theme;

    LIFTED_OBJECT(L"Microsoft.UI.Content.ContentIslandEnvironment")
    LIFTED_QI(IslandEnvironment, IContentIslandEnvironment, IContentIslandEnvironment2, IContentIslandEnvironmentExperimental)
    HRESULT STDMETHODCALLTYPE get_AppWindowId(mui::WindowId *value) override;
    HRESULT STDMETHODCALLTYPE get_DisplayId(mui::DisplayId *value) override;
    HRESULT STDMETHODCALLTYPE get_DisplayScale(FLOAT *value) override { *value = 1.0f; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CurrentOrientation(ContentDisplayOrientations *value) override
    {
        *value = ContentDisplayOrientations_Landscape;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_StateChanged(EnvStateHandler *h, EventRegistrationToken *t) override { return m_state.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_StateChanged(EventRegistrationToken t) override { return m_state.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_SettingChanged(EnvSettingHandler *h, EventRegistrationToken *t) override { return m_setting.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_SettingChanged(EventRegistrationToken t) override { return m_setting.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_ThemeChanged(EnvThemeHandler *h, EventRegistrationToken *t) override { return m_theme.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_ThemeChanged(EventRegistrationToken t) override { return m_theme.Remove(t); }
};

/* ---- ContentCoordinateConverter ------------------------------------------ */
struct CoordinateConverter : Object, Stub<IContentCoordinateConverter>
{
    Island *m_island;            /* (a reference) */
    LIFTED_OBJECT(L"Microsoft.UI.Content.ContentCoordinateConverter")
    LIFTED_QI(CoordinateConverter, IContentCoordinateConverter)
    ~CoordinateConverter();
    POINT origin();
    HRESULT STDMETHODCALLTYPE ConvertLocalToScreenWithPoint(wf::Point p, ABI::Windows::Graphics::PointInt32 *r) override
    {
        POINT o = origin();
        r->X = o.x + (INT32)(p.X + 0.5f);
        r->Y = o.y + (INT32)(p.Y + 0.5f);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ConvertLocalToScreenWithRect(wf::Rect rc, ABI::Windows::Graphics::RectInt32 *r) override
    {
        POINT o = origin();
        r->X = o.x + (INT32)(rc.X + 0.5f);
        r->Y = o.y + (INT32)(rc.Y + 0.5f);
        r->Width = (INT32)(rc.Width + 0.5f);
        r->Height = (INT32)(rc.Height + 0.5f);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ConvertScreenToLocalWithPoint(ABI::Windows::Graphics::PointInt32 p, wf::Point *r) override
    {
        POINT o = origin();
        r->X = (FLOAT)(p.X - o.x);
        r->Y = (FLOAT)(p.Y - o.y);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ConvertScreenToLocalWithRect(ABI::Windows::Graphics::RectInt32 rc, wf::Rect *r) override
    {
        POINT o = origin();
        r->X = (FLOAT)(rc.X - o.x);
        r->Y = (FLOAT)(rc.Y - o.y);
        r->Width = (FLOAT)rc.Width;
        r->Height = (FLOAT)rc.Height;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ConvertLocalToScreenWithPoints(UINT32 n, wf::Point *pts, UINT32 *rn,
                                                             ABI::Windows::Graphics::PointInt32 **r) override
    {
        *rn = 0;
        *r = (ABI::Windows::Graphics::PointInt32 *)CoTaskMemAlloc((n ? n : 1) * sizeof **r);
        if (!*r) return E_OUTOFMEMORY;
        for (UINT32 i = 0; i < n; i++) ConvertLocalToScreenWithPoint(pts[i], &(*r)[i]);
        *rn = n;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ConvertLocalToScreenWithPointsAndRoundingMode(UINT32 n, wf::Point *pts, ContentCoordinateRoundingMode,
                                                                            UINT32 *rn, ABI::Windows::Graphics::PointInt32 **r) override
    {
        return ConvertLocalToScreenWithPoints(n, pts, rn, r);
    }
    HRESULT STDMETHODCALLTYPE ConvertScreenToLocalWithPoints(UINT32 n, ABI::Windows::Graphics::PointInt32 *pts, UINT32 *rn,
                                                             wf::Point **r) override
    {
        *rn = 0;
        *r = (wf::Point *)CoTaskMemAlloc((n ? n : 1) * sizeof **r);
        if (!*r) return E_OUTOFMEMORY;
        for (UINT32 i = 0; i < n; i++) ConvertScreenToLocalWithPoint(pts[i], &(*r)[i]);
        *rn = n;
        return S_OK;
    }
};

/* ---- Island ---------------------------------------------------------- */
struct Island : Object, Stub<IContentIsland>, Stub<IContentIsland2>, IContentIslandExperimental,
                       Stub<IContentIslandAutomation>, wf::IClosable, Stub<mui::IClosableNotifier>
{
    Island *m_next;
    UINT64 m_id;
    DWORD m_thread;
    mcomp::IVisual *m_root = nullptr;
    mcomp::ICompositor *m_compositor = nullptr;
    IInspectable *m_appdata = nullptr;
    IslandEnvironment *m_env = nullptr;
    InputSources *m_inputs = nullptr;
    SiteBridge *m_bridge = nullptr;     /* (not a reference: the bridge holds the island) */
    wfn::Vector2 m_size = {};
    bool m_island_visible = true, m_island_enabled = true, m_hit_test_transparent = false, m_closed = false;
    Event<IslandStateHandler> m_state;
    Event<IslandAutomationHandler> m_automation;
    Event<mui::IClosableNotifierHandler> m_closed_ev, m_framework_closed;

    LIFTED_OBJECT(L"Microsoft.UI.Content.ContentIsland")
    LIFTED_QI(Island, IContentIsland, IContentIsland2, IContentIslandExperimental, IContentIslandAutomation,
              wf::IClosable, mui::IClosableNotifier)

    ~Island()
    {
        Close();
        if (m_inputs) inputs_destroy(m_inputs);
        if (m_env) m_env->Release();
        set_ref(m_root, (mcomp::IVisual *)nullptr);
        set_ref(m_compositor, (mcomp::ICompositor *)nullptr);
        set_ref(m_appdata, (IInspectable *)nullptr);
    }

    void raise_state(bool size, bool visible, bool enabled, bool scale)
    {
        IslandStateArgs *a = new IslandStateArgs();
        if (!a) return;
        a->size = size;
        a->visible = visible;
        a->enabled = enabled;
        a->scale = scale;
        m_state.Raise(static_cast<IContentIsland *>(this), static_cast<IContentIslandStateChangedEventArgs *>(a));
        a->Release();
    }

    HRESULT STDMETHODCALLTYPE get_ActualSize(wfn::Vector2 *v) override { *v = m_size; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_AppData(IInspectable **v) override { return copy_out(m_appdata, v); }
    HRESULT STDMETHODCALLTYPE put_AppData(IInspectable *v) override { set_ref(m_appdata, v); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CoordinateConverter(IContentCoordinateConverter **v) override
    {
        CoordinateConverter *c = new CoordinateConverter();
        if (!c) return E_OUTOFMEMORY;
        c->m_island = this;
        AddRef();
        *v = c;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_DispatcherQueue(ABI::Microsoft::UI::Dispatching::IDispatcherQueue **v) override;
    HRESULT STDMETHODCALLTYPE get_Environment(IContentIslandEnvironment **v) override { return copy_out<IContentIslandEnvironment>(m_env, v); }
    HRESULT STDMETHODCALLTYPE get_Id(UINT64 *v) override { *v = m_id; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsConnected(::boolean *v) override { *v = m_bridge != nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsHitTestVisibleWhenTransparent(::boolean *v) override { *v = m_hit_test_transparent; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsHitTestVisibleWhenTransparent(::boolean v) override { m_hit_test_transparent = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsIslandEnabled(::boolean *v) override { *v = m_island_enabled; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsIslandEnabled(::boolean v) override { m_island_enabled = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsIslandVisible(::boolean *v) override { *v = m_island_visible; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_IsIslandVisible(::boolean v) override;
    HRESULT STDMETHODCALLTYPE get_IsSiteEnabled(::boolean *v) override;
    HRESULT STDMETHODCALLTYPE get_IsSiteVisible(::boolean *v) override;
    HRESULT STDMETHODCALLTYPE get_LayoutDirection(ContentLayoutDirection *v) override
    {
        HWND w = island_window(this);
        *v = w && (GetWindowLongW(w, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) ? ContentLayoutDirection_RightToLeft
                                                                      : ContentLayoutDirection_LeftToRight;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RasterizationScale(FLOAT *v) override { *v = 1.0f; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetAutomationHostProvider(IInspectable **r) override { *r = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE RequestSize(wfn::Vector2) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE add_StateChanged(IslandStateHandler *h, EventRegistrationToken *t) override { return m_state.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_StateChanged(EventRegistrationToken t) override { return m_state.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_AutomationProviderRequested(IslandAutomationHandler *h, EventRegistrationToken *t) override
    {
        return m_automation.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_AutomationProviderRequested(EventRegistrationToken t) override { return m_automation.Remove(t); }

    /* IContentIsland2 */
    HRESULT STDMETHODCALLTYPE get_LocalToClientTransformMatrix(wfn::Matrix4x4 *v) override { identity(v); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_LocalToParentTransformMatrix(wfn::Matrix4x4 *v) override { identity(v); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ProcessesKeyboardInput(::boolean *v) override { *v = true; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ProcessesPointerInput(::boolean *v) override { *v = true; return S_OK; }
    static void identity(wfn::Matrix4x4 *m)
    {
        memset(m, 0, sizeof *m);
        m->M11 = m->M22 = m->M33 = m->M44 = 1.0f;
    }

    /* IContentIslandExperimental: a new root, shown by the bridge's target */
    HRESULT STDMETHODCALLTYPE put_Root(mcomp::IVisual *value) override;

    /* IContentIslandAutomation */
    HRESULT STDMETHODCALLTYPE get_AutomationOption(ContentAutomationOptions *v) override
    {
        *v = ContentAutomationOptions_FragmentBased;
        return S_OK;
    }

    /* IClosable, IClosableNotifier */
    HRESULT STDMETHODCALLTYPE Close() override
    {
        if (m_closed) return S_OK;
        m_closed = true;
        AcquireSRWLockExclusive(&g_islands_lock);
        for (Island **p = &g_islands; *p; p = &(*p)->m_next)
            if (*p == this) { *p = m_next; break; }
        ReleaseSRWLockExclusive(&g_islands_lock);
        m_framework_closed.Raise();
        m_closed_ev.Raise();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsClosed(::boolean *v) override { *v = m_closed; return S_OK; }
    HRESULT STDMETHODCALLTYPE add_Closed(mui::IClosableNotifierHandler *h, EventRegistrationToken *t) override { return m_closed_ev.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_Closed(EventRegistrationToken t) override { return m_closed_ev.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_FrameworkClosed(mui::IClosableNotifierHandler *h, EventRegistrationToken *t) override
    {
        return m_framework_closed.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_FrameworkClosed(EventRegistrationToken t) override { return m_framework_closed.Remove(t); }
};

CoordinateConverter::~CoordinateConverter() { if (m_island) static_cast<IContentIsland *>(m_island)->Release(); }

/* ---- SiteBridge ------------------------------------------------- */
#define BRIDGE_CLASS W(L"Microsoft.UI.Content.DesktopChildSiteBridge")

struct SiteBridge : Object, Stub<IDesktopChildSiteBridge>, Stub<IDesktopSiteBridge>, Stub<IDesktopSiteBridge2>,
                                Stub<IContentSiteBridge>, wf::IClosable, Stub<mui::IClosableNotifier>
{
    HWND m_hwnd = nullptr;
    mcomp::ICompositor *m_compositor = nullptr;
    Island *m_island = nullptr;              /* (a reference) */
    IInspectable *m_target = nullptr;               /* the composition target on m_hwnd */
    ContentSizePolicy m_policy = ContentSizePolicy_ResizeContentToParentWindow;
    FLOAT m_override_scale = 0.0f;
    bool m_closed = false;
    Event<mui::IClosableNotifierHandler> m_closed_ev, m_framework_closed;

    LIFTED_OBJECT(L"Microsoft.UI.Content.DesktopChildSiteBridge")
    LIFTED_QI(SiteBridge, IDesktopChildSiteBridge, IDesktopSiteBridge, IDesktopSiteBridge2, IContentSiteBridge,
              wf::IClosable, mui::IClosableNotifier)
    ~SiteBridge()
    {
        Close();
        set_ref(m_compositor, (mcomp::ICompositor *)nullptr);
    }

    /* the target shows the island's root */
    void show_root()
    {
        if (!m_target || !m_island) return;
        ABI::Windows::UI::Composition::ICompositionTarget *t = nullptr;
        if (FAILED(m_target->QueryInterface(__uuidof(ABI::Windows::UI::Composition::ICompositionTarget), (void **)&t))) return;
        ABI::Windows::UI::Composition::IVisual *v = nullptr;
        if (m_island->m_root)
            m_island->m_root->QueryInterface(__uuidof(ABI::Windows::UI::Composition::IVisual), (void **)&v);
        t->put_Root(v);
        if (v) v->Release();
        t->Release();
    }
    /* the window's size is the island's */
    void sized()
    {
        if (!m_island) return;
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        wfn::Vector2 s = { (FLOAT)(rc.right - rc.left), (FLOAT)(rc.bottom - rc.top) };
        if (s.X == m_island->m_size.X && s.Y == m_island->m_size.Y) return;
        m_island->m_size = s;
        m_island->raise_state(true, false, false, false);
    }

    static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        SiteBridge *self = (SiteBridge *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (self && self->m_island && self->m_island->m_inputs) {
            LRESULT r = 0;
            if (inputs_message(self->m_island->m_inputs, hwnd, msg, wp, lp, &r)) return r;
        }
        switch (msg) {
        case WM_SIZE:
            if (self) self->sized();
            break;
        case WM_SHOWWINDOW:
            if (self && self->m_island) {
                PostMessageW(hwnd, WM_APP, 0, 0);       /* (raised once the window's visibility has changed) */
            }
            break;
        case WM_APP:
            if (self && self->m_island) self->m_island->raise_state(false, true, false, false);
            return 0;
        case WM_ERASEBKGND:
            return 1;                                   /* (the compositor draws all of it) */
        case WM_NCDESTROY:
            if (self) self->m_hwnd = nullptr;
            break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    /* IDesktopChildSiteBridge */
    HRESULT STDMETHODCALLTYPE get_ResizePolicy(ContentSizePolicy *v) override { *v = m_policy; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_ResizePolicy(ContentSizePolicy v) override { m_policy = v; return S_OK; }

    /* IDesktopSiteBridge */
    HRESULT STDMETHODCALLTYPE get_IsEnabled(::boolean *v) override { *v = m_hwnd && IsWindowEnabled(m_hwnd); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsVisible(::boolean *v) override { *v = m_hwnd && IsWindowVisible(m_hwnd); return S_OK; }
    HRESULT STDMETHODCALLTYPE get_WindowId(mui::WindowId *v) override { v->Value = (UINT64)(ULONG_PTR)m_hwnd; return S_OK; }
    HRESULT STDMETHODCALLTYPE Connect(IContentIsland *content) override
    {
        if (m_closed || !m_hwnd) return RO_E_CLOSED;
        if (m_island) return E_ILLEGAL_METHOD_CALL;
        IContentIslandExperimental *exp = nullptr;      /* (only NovaOS's own islands answer it) */
        if (!content || FAILED(content->QueryInterface(__uuidof(IContentIslandExperimental), (void **)&exp)) || !exp)
            return E_INVALIDARG;
        m_island = static_cast<Island *>(exp);
        if (m_island->m_bridge) {
            m_island->Release();
            m_island = nullptr;
            return E_ILLEGAL_METHOD_CALL;
        }
        m_island->m_bridge = this;
        ICompositorDesktopInterop *interop = nullptr;
        HRESULT hr = m_compositor ? m_compositor->QueryInterface(__uuidof(ICompositorDesktopInterop), (void **)&interop) : E_FAIL;
        if (SUCCEEDED(hr)) {
            hr = interop->CreateDesktopWindowTarget(m_hwnd, FALSE, &m_target);
            interop->Release();
        }
        if (FAILED(hr)) return hr;
        show_root();
        sized();
        m_island->raise_state(true, true, true, true);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Disable() override { if (m_hwnd) EnableWindow(m_hwnd, FALSE); return S_OK; }
    HRESULT STDMETHODCALLTYPE Enable() override { if (m_hwnd) EnableWindow(m_hwnd, TRUE); return S_OK; }
    HRESULT STDMETHODCALLTYPE Hide() override { if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE); return S_OK; }
    HRESULT STDMETHODCALLTYPE Show() override { if (m_hwnd) ShowWindow(m_hwnd, SW_SHOWNA); return S_OK; }
    HRESULT STDMETHODCALLTYPE MoveAndResize(ABI::Windows::Graphics::RectInt32 r) override
    {
        if (m_hwnd) SetWindowPos(m_hwnd, nullptr, r.X, r.Y, r.Width, r.Height, SWP_NOZORDER | SWP_NOACTIVATE);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveInZOrderAtBottom() override
    {
        if (m_hwnd) SetWindowPos(m_hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveInZOrderAtTop() override
    {
        if (m_hwnd) SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveInZOrderBelow(mui::WindowId id) override
    {
        if (m_hwnd) SetWindowPos(m_hwnd, (HWND)(ULONG_PTR)id.Value, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return S_OK;
    }

    /* IContentSiteBridge */
    HRESULT STDMETHODCALLTYPE get_DispatcherQueue(ABI::Microsoft::UI::Dispatching::IDispatcherQueue **v) override;
    HRESULT STDMETHODCALLTYPE get_OverrideScale(FLOAT *v) override { *v = m_override_scale; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_OverrideScale(FLOAT v) override { m_override_scale = v; return S_OK; }

    /* IClosable, IClosableNotifier: the window goes, the island stays */
    HRESULT STDMETHODCALLTYPE Close() override
    {
        if (m_closed) return S_OK;
        m_closed = true;
        set_ref(m_target, (IInspectable *)nullptr);
        if (m_island) {
            m_island->m_bridge = nullptr;
            Island *i = m_island;
            m_island = nullptr;
            static_cast<IContentIsland *>(i)->Release();
        }
        if (m_hwnd) {
            SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
        m_framework_closed.Raise();
        m_closed_ev.Raise();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsClosed(::boolean *v) override { *v = m_closed; return S_OK; }
    HRESULT STDMETHODCALLTYPE add_Closed(mui::IClosableNotifierHandler *h, EventRegistrationToken *t) override { return m_closed_ev.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_Closed(EventRegistrationToken t) override { return m_closed_ev.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_FrameworkClosed(mui::IClosableNotifierHandler *h, EventRegistrationToken *t) override
    {
        return m_framework_closed.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_FrameworkClosed(EventRegistrationToken t) override { return m_framework_closed.Remove(t); }
};

/* ---- the island's pieces that need the bridge ------------------------------ */
HWND island_window(IContentIsland *island)
{
    Island *i = static_cast<Island *>(island);
    return i && i->m_bridge ? i->m_bridge->m_hwnd : nullptr;
}

InputSources *island_inputs(IContentIsland *island)
{
    return island ? static_cast<Island *>(island)->m_inputs : nullptr;
}

float island_scale(IContentIsland *) { return 1.0f; }

POINT CoordinateConverter::origin()
{
    POINT p = { 0, 0 };
    HWND w = island_window(m_island);
    if (w) ClientToScreen(w, &p);
    return p;
}

HRESULT IslandEnvironment::get_AppWindowId(mui::WindowId *value)
{
    HWND w = island_window(m_island);
    value->Value = w ? (UINT64)(ULONG_PTR)GetAncestor(w, GA_ROOT) : 0;
    return S_OK;
}

HRESULT IslandEnvironment::get_DisplayId(mui::DisplayId *value)
{
    HWND w = island_window(m_island);
    value->Value = (UINT64)(ULONG_PTR)MonitorFromWindow(w, MONITOR_DEFAULTTOPRIMARY);
    return S_OK;
}

HRESULT Island::put_IsIslandVisible(::boolean v)
{
    m_island_visible = v;
    if (m_root) m_root->put_IsVisible(v);
    return S_OK;
}

HRESULT Island::get_IsSiteEnabled(::boolean *v)
{
    HWND w = island_window(this);
    *v = w && IsWindowEnabled(w);
    return S_OK;
}

HRESULT Island::get_IsSiteVisible(::boolean *v)
{
    HWND w = island_window(this);
    *v = w && IsWindowVisible(w);
    return S_OK;
}

HRESULT Island::put_Root(mcomp::IVisual *value)
{
    set_ref(m_root, value);
    if (m_bridge) m_bridge->show_root();
    return S_OK;
}

/* the calling thread's DispatcherQueue (CoreMessaging.dll's), or none */
static HRESULT current_queue(ABI::Microsoft::UI::Dispatching::IDispatcherQueue **v)
{
    *v = nullptr;
    ABI::Microsoft::UI::Dispatching::IDispatcherQueueStatics *s = nullptr;
    HSTRING name = nullptr;
    make_hstring(L"Microsoft.UI.Dispatching.DispatcherQueue", &name);
    HRESULT hr = RoGetActivationFactory(name, __uuidof(ABI::Microsoft::UI::Dispatching::IDispatcherQueueStatics), (void **)&s);
    WindowsDeleteString(name);
    if (FAILED(hr)) return hr;
    hr = s->GetForCurrentThread(v);
    s->Release();
    return hr;
}

HRESULT Island::get_DispatcherQueue(ABI::Microsoft::UI::Dispatching::IDispatcherQueue **v) { return current_queue(v); }
HRESULT SiteBridge::get_DispatcherQueue(ABI::Microsoft::UI::Dispatching::IDispatcherQueue **v) { return current_queue(v); }

/* ---- the statics -------------------------------------------------------------- */
struct ContentIslandStatics : Object, Stub<IContentIslandStatics>, IActivationFactory
{
    LIFTED_OBJECT(L"Microsoft.UI.Content.ContentIsland")
    LIFTED_QI(ContentIslandStatics, IContentIslandStatics, IActivationFactory)
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable **out) override { *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Create(mcomp::IVisual *root, IContentIsland **result) override
    {
        *result = nullptr;
        if (!root) return E_INVALIDARG;
        Island *i = new Island();
        if (!i) return E_OUTOFMEMORY;
        set_ref(i->m_root, root);
        mcomp::ICompositionObject *o = nullptr;
        if (SUCCEEDED(root->QueryInterface(__uuidof(mcomp::ICompositionObject), (void **)&o))) {
            o->get_Compositor(&i->m_compositor);
            o->Release();
        }
        i->m_thread = GetCurrentThreadId();
        i->m_env = new IslandEnvironment();
        if (i->m_env) i->m_env->m_island = i;
        i->m_inputs = inputs_create(i);
        AcquireSRWLockExclusive(&g_islands_lock);
        i->m_id = g_next_id++;
        i->m_next = g_islands;
        g_islands = i;
        ReleaseSRWLockExclusive(&g_islands_lock);
        *result = i;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetByVisual(mcomp::IVisual *child, IContentIsland **result) override
    {
        *result = nullptr;
        AcquireSRWLockShared(&g_islands_lock);
        for (Island *i = g_islands; i; i = i->m_next)
            if (i->m_root == child) {
                *result = i;
                i->AddRef();
                break;
            }
        ReleaseSRWLockShared(&g_islands_lock);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFromId(UINT64 id, IContentIsland **result) override
    {
        *result = nullptr;
        AcquireSRWLockShared(&g_islands_lock);
        for (Island *i = g_islands; i; i = i->m_next)
            if (i->m_id == id) {
                *result = i;
                i->AddRef();
                break;
            }
        ReleaseSRWLockShared(&g_islands_lock);
        return S_OK;
    }
    HRESULT find(bool (*match)(Island *, void *), void *ctx, UINT32 *n, IContentIsland ***result)
    {
        *n = 0;
        AcquireSRWLockShared(&g_islands_lock);
        UINT32 count = 0;
        for (Island *i = g_islands; i; i = i->m_next) count += match(i, ctx);
        *result = (IContentIsland **)CoTaskMemAlloc((count ? count : 1) * sizeof(IContentIsland *));
        if (*result)
            for (Island *i = g_islands; i; i = i->m_next)
                if (match(i, ctx)) {
                    i->AddRef();
                    (*result)[(*n)++] = i;
                }
        ReleaseSRWLockShared(&g_islands_lock);
        return *result ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE FindAllForCurrentThread(UINT32 *n, IContentIsland ***result) override
    {
        DWORD me = GetCurrentThreadId();
        return find([](Island *i, void *c) { return i->m_thread == *(DWORD *)c; }, &me, n, result);
    }
    HRESULT STDMETHODCALLTYPE FindAllForCompositor(mcomp::ICompositor *compositor, UINT32 *n, IContentIsland ***result) override
    {
        return find([](Island *i, void *c) { return (void *)i->m_compositor == c; }, compositor, n, result);
    }
};

struct DesktopChildSiteBridgeStatics : Object, Stub<IDesktopChildSiteBridgeStatics>, IActivationFactory
{
    LIFTED_OBJECT(L"Microsoft.UI.Content.DesktopChildSiteBridge")
    LIFTED_QI(DesktopChildSiteBridgeStatics, IDesktopChildSiteBridgeStatics, IActivationFactory)
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable **out) override { *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Create(mcomp::ICompositor *compositor, mui::WindowId parentWindowId, IDesktopChildSiteBridge **result) override
    {
        *result = nullptr;
        HWND parent = (HWND)(ULONG_PTR)parentWindowId.Value;
        if (!compositor || !IsWindow(parent)) return E_INVALIDARG;
        static LONG registered;
        if (!InterlockedExchange(&registered, 1)) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof wc;
            wc.lpfnWndProc = SiteBridge::wndproc;
            wc.hInstance = GetModuleHandleW(W(L"Microsoft.UI.Input.dll"));
            wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
            wc.lpszClassName = BRIDGE_CLASS;
            wc.style = CS_DBLCLKS;
            RegisterClassExW(&wc);
        }
        SiteBridge *b = new SiteBridge();
        if (!b) return E_OUTOFMEMORY;
        set_ref(b->m_compositor, compositor);
        b->m_hwnd = CreateWindowExW(0, BRIDGE_CLASS, W(L""), WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 0, 0, parent,
                                    nullptr, GetModuleHandleW(W(L"Microsoft.UI.Input.dll")), nullptr);
        if (!b->m_hwnd) {
            DWORD e = GetLastError();
            b->Release();
            return HRESULT_FROM_WIN32(e ? e : ERROR_INVALID_WINDOW_HANDLE);
        }
        SetWindowLongPtrW(b->m_hwnd, GWLP_USERDATA, (LONG_PTR)b);
        *result = b;
        return S_OK;
    }
};

struct CoordinateConverterStatics : Object, Stub<IContentCoordinateConverterStatics>, IActivationFactory
{
    LIFTED_OBJECT(L"Microsoft.UI.Content.ContentCoordinateConverter")
    LIFTED_QI(CoordinateConverterStatics, IContentCoordinateConverterStatics, IActivationFactory)
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable **out) override { *out = nullptr; return E_NOTIMPL; }
};

IActivationFactory *content_island_factory() { return new ContentIslandStatics(); }
IActivationFactory *desktop_child_site_bridge_factory() { return new DesktopChildSiteBridgeStatics(); }
IActivationFactory *content_coordinate_converter_factory() { return new CoordinateConverterStatics(); }
