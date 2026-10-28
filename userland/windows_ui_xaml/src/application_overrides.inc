// NovaOS: the Windows.UI.Xaml faces of Application's overridable methods,
// which winfaces.py cannot match (included by windows_faces.cpp).  Windows'
// IApplicationOverrides has the UWP activations, OnLaunched with
// Windows.ApplicationModel.Activation's LaunchActivatedEventArgs and
// OnWindowCreated; the framework splits them into IApplicationOverrides
// (OnLaunched with its own LaunchActivatedEventArgs, which carries the UWP
// one) and IApplicationOverridesFeature_UwpSupportApi (the rest, with
// Windows' argument types but its own WindowCreatedEventArgs).  So:
//   - the framework calling a UWP app's overrides (a .NET or C++/WinRT
//     Application subclass: Calculator's OnLaunched) goes through two
//     override faces over the app's Windows IApplicationOverrides (and
//     IApplicationOverrides2's OnBackgroundActivated);
//   - the app calling its base class's (the framework's: each does
//     nothing) goes through forward faces that answer S_OK.

// Windows.UI.Xaml.IApplicationOverrides, IApplicationOverrides2
static const GUID IID_WuxAppOverrides = {0x25f99ff7, 0x9347, 0x459a, {0x9f, 0xac, 0xb2, 0xd0, 0xe1, 0x1c, 0x1a, 0x0f}};
static const GUID IID_WuxAppOverrides2 = {0xdb5cd2b9, 0xd3b4, 0x558c, {0xc6, 0x4e, 0x04, 0x34, 0xfd, 0x1b, 0xd8, 0x89}};
// Microsoft.UI.Xaml.IApplicationOverrides, IApplicationOverridesFeature_UwpSupportApi
static const GUID IID_MuxAppOverrides = {0xa33e81ef, 0xc665, 0x503b, {0x88, 0x27, 0xd2, 0x7e, 0xf1, 0x72, 0x0a, 0x06}};
static const GUID IID_MuxAppOverridesUwp = {0xbe225a8b, 0x628d, 0x518c, {0x86, 0x32, 0x6a, 0x7a, 0xa5, 0xba, 0x2b, 0xae}};
// Windows.UI.Xaml.IWindowCreatedEventArgs
static const GUID IID_WuxWindowCreatedEventArgs = {0x31b71470, 0xfeff, 0x4654, {0xaf, 0x48, 0x9b, 0x39, 0x8a, 0xb5, 0x77, 0x2b}};

extern "C" HRESULT STDMETHODCALLTYPE nova_tear_qi(NovaTear *t, REFIID iid, void **out);
extern "C" ULONG STDMETHODCALLTYPE nova_tear_addref(NovaTear *t);
extern "C" ULONG STDMETHODCALLTYPE nova_tear_release(NovaTear *t);
extern "C" HRESULT STDMETHODCALLTYPE nova_tear_iids(NovaTear *t, ULONG *count, IID **iids);
extern "C" HRESULT STDMETHODCALLTYPE nova_tear_name(NovaTear *t, void **name);
extern "C" HRESULT STDMETHODCALLTYPE nova_tear_trust(NovaTear *, INT32 *level);

// the app's override at @slot of its Windows interface @iid, with @arg
static HRESULT app_override(NovaTear *t, const GUID *iid, unsigned slot, void *arg)
{
    void *p;
    HRESULT hr = qi(t->obj, iid, &p);
    if (FAILED(hr))
        return iid == &IID_WuxAppOverrides2 ? S_OK : hr;    // (an app from before OnBackgroundActivated)
    hr = ((HRESULT (STDMETHODCALLTYPE *)(void *, void *))VT(p)[slot])(p, arg);
    nova_release(p);
    return hr;
}

// the framework's OnLaunched: the app's, with the UWP arguments its own carry
static HRESULT STDMETHODCALLTYPE ao_launched(NovaTear *t, void *args)
{
    void *uwp = nullptr;
    HRESULT hr = args ? ((HRESULT (STDMETHODCALLTYPE *)(void *, void **))VT(args)[7])(args, &uwp) : S_OK;
    if (SUCCEEDED(hr))
        hr = app_override(t, &IID_WuxAppOverrides, 7, uwp);
    nova_release(uwp);
    return hr;
}
#define AO_UWP(name, slot) \
    static HRESULT STDMETHODCALLTYPE name(NovaTear *t, void *args) { return app_override(t, &IID_WuxAppOverrides, slot, args); }
AO_UWP(ao_activated, 6)
AO_UWP(ao_file, 8)
AO_UWP(ao_search, 9)
AO_UWP(ao_share, 10)
AO_UWP(ao_open_picker, 11)
AO_UWP(ao_save_picker, 12)
AO_UWP(ao_file_updater, 13)
static HRESULT STDMETHODCALLTYPE ao_background(NovaTear *t, void *args)
{
    return app_override(t, &IID_WuxAppOverrides2, 6, args);
}
// (the framework's WindowCreatedEventArgs as Windows')
static HRESULT STDMETHODCALLTYPE ao_window_created(NovaTear *t, void *args)
{
    void *w = nullptr;
    HRESULT hr = args ? nova_cross(args, &IID_WuxWindowCreatedEventArgs, &w) : S_OK;
    if (SUCCEEDED(hr))
        hr = app_override(t, &IID_WuxAppOverrides, 14, w);
    nova_release(w);
    return hr;
}
static HRESULT STDMETHODCALLTYPE ao_base(NovaTear *, void *) { return S_OK; }

#define AO_UNKNOWN (void *)&nova_tear_qi, (void *)&nova_tear_addref, (void *)&nova_tear_release, \
    (void *)&nova_tear_iids, (void *)&nova_tear_name, (void *)&nova_tear_trust
static void *const s_ao_launched_vtbl[] = {AO_UNKNOWN, (void *)&ao_launched};
static void *const s_ao_uwp_vtbl[] = {AO_UNKNOWN, (void *)&ao_activated, (void *)&ao_file, (void *)&ao_search,
                                      (void *)&ao_share, (void *)&ao_open_picker, (void *)&ao_save_picker,
                                      (void *)&ao_file_updater, (void *)&ao_background, (void *)&ao_window_created};
static void *const s_ao_base_vtbl[] = {AO_UNKNOWN, (void *)&ao_base, (void *)&ao_base, (void *)&ao_base,
                                       (void *)&ao_base, (void *)&ao_base, (void *)&ao_base, (void *)&ao_base,
                                       (void *)&ao_base, (void *)&ao_base};

static const NovaFace s_hand_faces[] = {
    {&IID_MuxAppOverrides, &IID_WuxAppOverrides, s_ao_launched_vtbl, NOVA_FACE_REVERSE | NOVA_FACE_OVERRIDE,
     "Windows.UI.Xaml.IApplicationOverrides (framework OnLaunched)"},
    {&IID_MuxAppOverridesUwp, &IID_WuxAppOverrides, s_ao_uwp_vtbl, NOVA_FACE_REVERSE | NOVA_FACE_OVERRIDE,
     "Windows.UI.Xaml.IApplicationOverrides (framework UWP activations)"},
    {&IID_WuxAppOverrides, &IID_MuxAppOverrides, s_ao_base_vtbl, 0, "Windows.UI.Xaml.IApplicationOverrides"},
    {&IID_WuxAppOverrides2, &IID_MuxAppOverrides, s_ao_base_vtbl, 0, "Windows.UI.Xaml.IApplicationOverrides2"},
};
