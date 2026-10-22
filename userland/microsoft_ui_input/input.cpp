/*
 * Microsoft.UI.Input: an island's input sources, fed by its bridge's
 * window messages (content.cpp passes each message here first).
 *
 *   InputPointerSource      the mouse: PointerEntered/Moved/Exited (with
 *                           TrackMouseEvent's WM_MOUSELEAVE), Pressed and
 *                           Released (the window captures the mouse while
 *                           a button is down, as the SDK's implicit capture
 *                           does, and a press focuses the window: the
 *                           default InputPointerActivationBehavior),
 *                           WheelChanged, CaptureLost; one pointer, id 1,
 *                           a mouse, its position in the island's DIPs (a
 *                           scale of 1.0); its Cursor is the window's
 *   InputKeyboardSource     KeyDown/KeyUp, SystemKeyDown/SystemKeyUp and
 *                           CharacterReceived from WM_KEYDOWN, WM_SYSKEYDOWN,
 *                           WM_KEYUP, WM_SYSKEYUP and WM_CHAR; key states
 *                           from user32
 *   InputFocusController    GotFocus/LostFocus as the window gains and
 *                           loses the keyboard focus; TrySetFocus focuses it
 *   InputActivationListener the window's top-level window being the active
 *                           one (checked as the bridge window gains and
 *                           loses focus and its top-level window activates)
 *   InputPreTranslateKeyboardSource   takes a handler (its interop
 *                           interface); NovaOS's message loop does not call
 *                           it yet
 * and, for any window, InputNonClientPointerSource (the region rectangles a
 * window's title bar sets, kept), InputSystemCursor and FocusNavigationRequest.
 * A handled event's message is consumed (the window procedure returns 0).
 */
#include "lifted.h"

using namespace ixi;
namespace wsys = ABI::Windows::System;

typedef wf::ITypedEventHandler<ixi::InputPointerSource *, PointerEventArgs *> PointerHandler;
typedef wf::ITypedEventHandler<InputKeyboardSource *, KeyEventArgs *> KeyHandler;
typedef wf::ITypedEventHandler<InputKeyboardSource *, CharacterReceivedEventArgs *> CharHandler;
typedef wf::ITypedEventHandler<InputKeyboardSource *, ContextMenuKeyEventArgs *> ContextMenuHandler;
typedef wf::ITypedEventHandler<InputFocusController *, FocusChangedEventArgs *> FocusHandler;
typedef wf::ITypedEventHandler<InputFocusController *, FocusNavigationRequestEventArgs *> NavigateHandler;
typedef wf::ITypedEventHandler<InputActivationListener *, InputActivationListenerActivationChangedEventArgs *> ActivationHandler;

static wsys::VirtualKeyModifiers modifiers()
{
    unsigned m = 0;
    if (GetKeyState(VK_CONTROL) < 0) m |= wsys::VirtualKeyModifiers_Control;
    if (GetKeyState(VK_MENU) < 0) m |= wsys::VirtualKeyModifiers_Menu;
    if (GetKeyState(VK_SHIFT) < 0) m |= wsys::VirtualKeyModifiers_Shift;
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) m |= wsys::VirtualKeyModifiers_Windows;
    return (wsys::VirtualKeyModifiers)m;
}

/* ---- pointer points and their events' arguments --------------------------- */
struct PointerProperties : Object, Stub<IPointerPointProperties>
{
    WPARAM m_keys;                      /* (MK_* of the message) */
    PointerUpdateKind m_kind;
    INT32 m_wheel;
    bool m_hwheel;
    LIFTED_OBJECT(L"Microsoft.UI.Input.PointerPointProperties")
    LIFTED_QI(PointerProperties, IPointerPointProperties)
    HRESULT STDMETHODCALLTYPE get_IsInRange(::boolean *v) override { *v = true; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsPrimary(::boolean *v) override { *v = true; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsLeftButtonPressed(::boolean *v) override { *v = (m_keys & MK_LBUTTON) != 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsRightButtonPressed(::boolean *v) override { *v = (m_keys & MK_RBUTTON) != 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsMiddleButtonPressed(::boolean *v) override { *v = (m_keys & MK_MBUTTON) != 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsXButton1Pressed(::boolean *v) override { *v = (m_keys & MK_XBUTTON1) != 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsXButton2Pressed(::boolean *v) override { *v = (m_keys & MK_XBUTTON2) != 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsHorizontalMouseWheel(::boolean *v) override { *v = m_hwheel; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_MouseWheelDelta(INT32 *v) override { *v = m_wheel; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_PointerUpdateKind(PointerUpdateKind *v) override { *v = m_kind; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Pressure(FLOAT *v) override { *v = 0.5f; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_TouchConfidence(::boolean *v) override { *v = false; return S_OK; }
};

struct PtrPoint : Object, Stub<IPointerPoint>
{
    wf::Point m_pos;
    UINT64 m_time;
    UINT32 m_frame;
    PointerProperties *m_props;
    LIFTED_OBJECT(L"Microsoft.UI.Input.PointerPoint")
    LIFTED_QI(PtrPoint, IPointerPoint)
    ~PtrPoint() { if (m_props) m_props->Release(); }
    HRESULT STDMETHODCALLTYPE get_FrameId(UINT32 *v) override { *v = m_frame; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsInContact(::boolean *v) override
    {
        *v = (m_props->m_keys & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2)) != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PointerDeviceType(PointerDeviceType *v) override { *v = PointerDeviceType_Mouse; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_PointerId(UINT32 *v) override { *v = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Position(wf::Point *v) override { *v = m_pos; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Properties(IPointerPointProperties **v) override { return copy_out<IPointerPointProperties>(m_props, v); }
    HRESULT STDMETHODCALLTYPE get_Timestamp(UINT64 *v) override { *v = m_time; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTransformedPoint(IPointerPointTransform *transform, IPointerPoint **result) override
    {
        *result = nullptr;
        if (!transform) return E_INVALIDARG;
        PtrPoint *p = new PtrPoint();
        if (!p) return E_OUTOFMEMORY;
        p->m_time = m_time;
        p->m_frame = m_frame;
        set_ref(p->m_props, m_props);
        ::boolean ok = false;
        if (FAILED(transform->TryTransform(m_pos, &p->m_pos, &ok)) || !ok) p->m_pos = m_pos;
        *result = p;
        return S_OK;
    }
};

struct PointerArgs : Object, Stub<IPointerEventArgs>
{
    PtrPoint *m_point;
    wsys::VirtualKeyModifiers m_mods;
    ::boolean m_handled = false;
    LIFTED_OBJECT(L"Microsoft.UI.Input.PointerEventArgs")
    LIFTED_QI(PointerArgs, IPointerEventArgs)
    ~PointerArgs() { if (m_point) m_point->Release(); }
    HRESULT STDMETHODCALLTYPE get_CurrentPoint(IPointerPoint **v) override { return copy_out<IPointerPoint>(m_point, v); }
    HRESULT STDMETHODCALLTYPE get_Handled(::boolean *v) override { *v = m_handled; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Handled(::boolean v) override { m_handled = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_KeyModifiers(wsys::VirtualKeyModifiers *v) override { *v = m_mods; return S_OK; }
};

/* ---- keyboard events' arguments ------------------------------------------- */
static PhysicalKeyStatus key_status(LPARAM lp, bool released)
{
    PhysicalKeyStatus s = {};
    s.RepeatCount = (UINT32)(lp & 0xFFFF);
    s.ScanCode = (UINT32)((lp >> 16) & 0xFF);
    s.IsExtendedKey = (lp >> 24) & 1;
    s.IsMenuKeyDown = (lp >> 29) & 1;
    s.WasKeyDown = (lp >> 30) & 1;
    s.IsKeyReleased = released;
    return s;
}

struct KeyArgs : Object, Stub<IKeyEventArgs>
{
    wsys::VirtualKey m_key;
    PhysicalKeyStatus m_status;
    UINT64 m_time;
    ::boolean m_handled = false;
    LIFTED_OBJECT(L"Microsoft.UI.Input.KeyEventArgs")
    LIFTED_QI(KeyArgs, IKeyEventArgs)
    HRESULT STDMETHODCALLTYPE get_Handled(::boolean *v) override { *v = m_handled; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Handled(::boolean v) override { m_handled = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_KeyStatus(PhysicalKeyStatus *v) override { *v = m_status; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Timestamp(UINT64 *v) override { *v = m_time; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_VirtualKey(wsys::VirtualKey *v) override { *v = m_key; return S_OK; }
};

struct CharArgs : Object, Stub<ICharacterReceivedEventArgs>
{
    UINT32 m_code;
    PhysicalKeyStatus m_status;
    ::boolean m_handled = false;
    LIFTED_OBJECT(L"Microsoft.UI.Input.CharacterReceivedEventArgs")
    LIFTED_QI(CharArgs, ICharacterReceivedEventArgs)
    HRESULT STDMETHODCALLTYPE get_Handled(::boolean *v) override { *v = m_handled; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Handled(::boolean v) override { m_handled = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_KeyCode(UINT32 *v) override { *v = m_code; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_KeyStatus(PhysicalKeyStatus *v) override { *v = m_status; return S_OK; }
};

struct FocusArgs : Object, Stub<IFocusChangedEventArgs>
{
    ::boolean m_handled = false;
    LIFTED_OBJECT(L"Microsoft.UI.Input.FocusChangedEventArgs")
    LIFTED_QI(FocusArgs, IFocusChangedEventArgs)
    HRESULT STDMETHODCALLTYPE get_Handled(::boolean *v) override { *v = m_handled; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_Handled(::boolean v) override { m_handled = v; return S_OK; }
};

struct ActivationArgs : Object, Stub<IInputActivationListenerActivationChangedEventArgs>
{
    LIFTED_OBJECT(L"Microsoft.UI.Input.InputActivationListenerActivationChangedEventArgs")
    LIFTED_QI(ActivationArgs, IInputActivationListenerActivationChangedEventArgs)
};

/* ---- cursors ---------------------------------------------------------------- */
struct SystemCursor : Object, Stub<IInputSystemCursor>, Stub<IInputCursor>
{
    InputSystemCursorShape m_shape;
    LIFTED_OBJECT(L"Microsoft.UI.Input.InputSystemCursor")
    LIFTED_QI(SystemCursor, IInputSystemCursor, IInputCursor)
    HRESULT STDMETHODCALLTYPE get_CursorShape(InputSystemCursorShape *v) override { *v = m_shape; return S_OK; }
    HCURSOR load() const
    {
        static const struct { InputSystemCursorShape shape; LPCWSTR id; } map[] = {
            { InputSystemCursorShape_Arrow, (LPCWSTR)IDC_ARROW }, { InputSystemCursorShape_Cross, (LPCWSTR)IDC_CROSS },
            { InputSystemCursorShape_Hand, (LPCWSTR)IDC_HAND }, { InputSystemCursorShape_Help, (LPCWSTR)IDC_HELP },
            { InputSystemCursorShape_IBeam, (LPCWSTR)IDC_IBEAM }, { InputSystemCursorShape_SizeAll, (LPCWSTR)IDC_SIZEALL },
            { InputSystemCursorShape_SizeNortheastSouthwest, (LPCWSTR)IDC_SIZENESW },
            { InputSystemCursorShape_SizeNorthSouth, (LPCWSTR)IDC_SIZENS },
            { InputSystemCursorShape_SizeNorthwestSoutheast, (LPCWSTR)IDC_SIZENWSE },
            { InputSystemCursorShape_SizeWestEast, (LPCWSTR)IDC_SIZEWE }, { InputSystemCursorShape_UniversalNo, (LPCWSTR)IDC_NO },
            { InputSystemCursorShape_UpArrow, (LPCWSTR)IDC_UPARROW }, { InputSystemCursorShape_Wait, (LPCWSTR)IDC_WAIT },
        };
        for (auto &m : map)
            if (m.shape == m_shape) return LoadCursorW(nullptr, m.id);
        return LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    }
};

/* ---- an island's sources ----------------------------------------------------- */
struct PointerSource : Object, Stub<IInputPointerSource>, Stub<IInputPointerSource2>
{
    InputSources *m_owner;
    IInputCursor *m_cursor = nullptr;
    InputPointerActivationBehavior m_activation = InputPointerActivationBehavior_Default;
    InputPointerSourceDeviceKinds m_kinds = InputPointerSourceDeviceKinds_Mouse;
    Event<PointerHandler> m_lost, m_entered, m_exited, m_moved, m_pressed, m_released, m_away, m_routed_released, m_to,
        m_wheel, m_dm_hit;
    LIFTED_OBJECT(L"Microsoft.UI.Input.InputPointerSource")
    LIFTED_QI(PointerSource, IInputPointerSource, IInputPointerSource2)
    ~PointerSource() { set_ref(m_cursor, (IInputCursor *)nullptr); }
    HRESULT STDMETHODCALLTYPE get_Cursor(IInputCursor **v) override { return copy_out(m_cursor, v); }
    HRESULT STDMETHODCALLTYPE put_Cursor(IInputCursor *v) override;
    HRESULT STDMETHODCALLTYPE get_DeviceKinds(InputPointerSourceDeviceKinds *v) override { *v = m_kinds; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ActivationBehavior(InputPointerActivationBehavior *v) override { *v = m_activation; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_ActivationBehavior(InputPointerActivationBehavior v) override { m_activation = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE TrySetDeviceKinds(InputPointerSourceDeviceKinds kinds, ::boolean *result) override
    {
        m_kinds = kinds;
        *result = true;
        return S_OK;
    }
#define EVENT(NAME, FIELD)                                                                                              \
    HRESULT STDMETHODCALLTYPE add_##NAME(PointerHandler *h, EventRegistrationToken *t) override { return FIELD.Add(h, t); } \
    HRESULT STDMETHODCALLTYPE remove_##NAME(EventRegistrationToken t) override { return FIELD.Remove(t); }
    EVENT(PointerCaptureLost, m_lost)
    EVENT(PointerEntered, m_entered)
    EVENT(PointerExited, m_exited)
    EVENT(PointerMoved, m_moved)
    EVENT(PointerPressed, m_pressed)
    EVENT(PointerReleased, m_released)
    EVENT(PointerRoutedAway, m_away)
    EVENT(PointerRoutedReleased, m_routed_released)
    EVENT(PointerRoutedTo, m_to)
    EVENT(PointerWheelChanged, m_wheel)
    EVENT(DirectManipulationHitTest, m_dm_hit)
#undef EVENT
};

struct KeyboardSource : Object, Stub<IInputKeyboardSource>, Stub<IInputKeyboardSource2>
{
    Event<KeyHandler> m_down, m_up, m_sysdown, m_sysup;
    Event<CharHandler> m_char;
    Event<ContextMenuHandler> m_menu;
    LIFTED_OBJECT(L"Microsoft.UI.Input.InputKeyboardSource")
    LIFTED_QI(KeyboardSource, IInputKeyboardSource, IInputKeyboardSource2)
    HRESULT STDMETHODCALLTYPE GetCurrentKeyState(wsys::VirtualKey key, VirtualKeyStates *result) override
    {
        SHORT s = GetAsyncKeyState((int)key);
        *result = (VirtualKeyStates)((s < 0 ? VirtualKeyStates_Down : 0) | (::GetKeyState((int)key) & 1 ? VirtualKeyStates_Locked : 0));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetKeyState(wsys::VirtualKey key, VirtualKeyStates *result) override
    {
        SHORT s = ::GetKeyState((int)key);
        *result = (VirtualKeyStates)((s < 0 ? VirtualKeyStates_Down : 0) | (s & 1 ? VirtualKeyStates_Locked : 0));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_KeyDown(KeyHandler *h, EventRegistrationToken *t) override { return m_down.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_KeyDown(EventRegistrationToken t) override { return m_down.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_KeyUp(KeyHandler *h, EventRegistrationToken *t) override { return m_up.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_KeyUp(EventRegistrationToken t) override { return m_up.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_SystemKeyDown(KeyHandler *h, EventRegistrationToken *t) override { return m_sysdown.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_SystemKeyDown(EventRegistrationToken t) override { return m_sysdown.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_SystemKeyUp(KeyHandler *h, EventRegistrationToken *t) override { return m_sysup.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_SystemKeyUp(EventRegistrationToken t) override { return m_sysup.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_CharacterReceived(CharHandler *h, EventRegistrationToken *t) override { return m_char.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_CharacterReceived(EventRegistrationToken t) override { return m_char.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_ContextMenuKey(ContextMenuHandler *h, EventRegistrationToken *t) override { return m_menu.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_ContextMenuKey(EventRegistrationToken t) override { return m_menu.Remove(t); }
};

struct PreTranslateSource : Object, Stub<IInputPreTranslateKeyboardSource>, IInputPreTranslateKeyboardSourceInterop
{
    IInputPreTranslateKeyboardSourceHandler *m_handler = nullptr;
    LIFTED_OBJECT(L"Microsoft.UI.Input.InputPreTranslateKeyboardSource")
    LIFTED_QI(PreTranslateSource, IInputPreTranslateKeyboardSource, IInputPreTranslateKeyboardSourceInterop)
    ~PreTranslateSource() { set_ref(m_handler, (IInputPreTranslateKeyboardSourceHandler *)nullptr); }
    HRESULT STDMETHODCALLTYPE SetPreTranslateHandler(IInputPreTranslateKeyboardSourceHandler *handler) override
    {
        set_ref(m_handler, handler);
        return S_OK;
    }
};

struct FocusController : Object, Stub<IInputFocusController>, Stub<IInputFocusController2>
{
    InputSources *m_owner;
    Event<FocusHandler> m_got, m_lost;
    Event<NavigateHandler> m_navigate;
    LIFTED_OBJECT(L"Microsoft.UI.Input.InputFocusController")
    LIFTED_QI(FocusController, IInputFocusController, IInputFocusController2)
    HRESULT STDMETHODCALLTYPE get_HasFocus(::boolean *v) override;
    HRESULT STDMETHODCALLTYPE TrySetFocus(::boolean *result) override;
    HRESULT STDMETHODCALLTYPE DepartFocus(IFocusNavigationRequest *, FocusNavigationResult *result) override
    {
        *result = FocusNavigationResult_NotMoved;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_GotFocus(FocusHandler *h, EventRegistrationToken *t) override { return m_got.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_GotFocus(EventRegistrationToken t) override { return m_got.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_LostFocus(FocusHandler *h, EventRegistrationToken *t) override { return m_lost.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_LostFocus(EventRegistrationToken t) override { return m_lost.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_NavigateFocusRequested(NavigateHandler *h, EventRegistrationToken *t) override { return m_navigate.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_NavigateFocusRequested(EventRegistrationToken t) override { return m_navigate.Remove(t); }
};

struct ActivationListener : Object, Stub<IInputActivationListener>
{
    InputSources *m_owner;
    InputActivationState m_last = InputActivationState_None;
    Event<ActivationHandler> m_changed;
    LIFTED_OBJECT(L"Microsoft.UI.Input.InputActivationListener")
    LIFTED_QI(ActivationListener, IInputActivationListener)
    InputActivationState state();
    HRESULT STDMETHODCALLTYPE get_State(InputActivationState *v) override { *v = m_last = state(); return S_OK; }
    HRESULT STDMETHODCALLTYPE add_InputActivationChanged(ActivationHandler *h, EventRegistrationToken *t) override { return m_changed.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_InputActivationChanged(EventRegistrationToken t) override { return m_changed.Remove(t); }
    void check()
    {
        InputActivationState s = state();
        if (s == m_last) return;
        m_last = s;
        ActivationArgs *a = new ActivationArgs();
        if (!a) return;
        m_changed.Raise(static_cast<IInputActivationListener *>(this), static_cast<IInputActivationListenerActivationChangedEventArgs *>(a));
        a->Release();
    }
};

struct InputSources
{
    ixp::IContentIsland *m_island;      /* (not a reference: the island owns this) */
    PointerSource *m_pointer;
    KeyboardSource *m_keyboard;
    PreTranslateSource *m_pretranslate;
    FocusController *m_focus;
    ActivationListener *m_activation;
    bool m_inside, m_captured;
    UINT32 m_frame;

    static void *operator new(size_t n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
    static void operator delete(void *p) { HeapFree(GetProcessHeap(), 0, p); }
};

HRESULT PointerSource::put_Cursor(IInputCursor *v)
{
    set_ref(m_cursor, v);
    HWND w = island_window(m_owner->m_island);
    POINT p;
    if (w && GetCursorPos(&p) && WindowFromPoint(p) == w) SendMessageW(w, WM_SETCURSOR, (WPARAM)w, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    return S_OK;
}

HRESULT FocusController::get_HasFocus(::boolean *v)
{
    HWND w = island_window(m_owner->m_island);
    *v = w && GetFocus() == w;
    return S_OK;
}

HRESULT FocusController::TrySetFocus(::boolean *result)
{
    HWND w = island_window(m_owner->m_island);
    if (w && GetFocus() != w) SetFocus(w);
    *result = w && GetFocus() == w;
    return S_OK;
}

InputActivationState ActivationListener::state()
{
    HWND w = island_window(m_owner->m_island);
    if (!w) return InputActivationState_Deactivated;
    return GetActiveWindow() == GetAncestor(w, GA_ROOT) ? InputActivationState_Activated : InputActivationState_Deactivated;
}

InputSources *inputs_create(ixp::IContentIsland *island)
{
    InputSources *s = new InputSources();
    if (!s) return nullptr;
    s->m_island = island;
    s->m_pointer = new PointerSource();
    s->m_keyboard = new KeyboardSource();
    s->m_pretranslate = new PreTranslateSource();
    s->m_focus = new FocusController();
    s->m_activation = new ActivationListener();
    if (!s->m_pointer || !s->m_keyboard || !s->m_pretranslate || !s->m_focus || !s->m_activation) {
        inputs_destroy(s);
        return nullptr;
    }
    s->m_pointer->m_owner = s->m_focus->m_owner = s->m_activation->m_owner = s;
    return s;
}

void inputs_destroy(InputSources *s)
{
    if (!s) return;
    if (s->m_pointer) { s->m_pointer->m_owner = nullptr; s->m_pointer->Release(); }
    if (s->m_keyboard) s->m_keyboard->Release();
    if (s->m_pretranslate) s->m_pretranslate->Release();
    if (s->m_focus) { s->m_focus->m_owner = nullptr; s->m_focus->Release(); }
    if (s->m_activation) { s->m_activation->m_owner = nullptr; s->m_activation->Release(); }
    delete s;
}

/* (the sources outlive their island only as dead objects: an owner of
 * nullptr; their getters then see no window) */
HWND island_window_of(InputSources *s) { return s ? island_window(s->m_island) : nullptr; }

template <class T, class I> static HRESULT give(T *obj, I **out)
{
    if (!out) return E_POINTER;
    *out = obj;
    if (!obj) return E_FAIL;
    obj->AddRef();
    return S_OK;
}
HRESULT input_pointer_source(InputSources *s, IInputPointerSource **out) { return give<PointerSource, IInputPointerSource>(s ? s->m_pointer : nullptr, out); }
HRESULT input_keyboard_source(InputSources *s, IInputKeyboardSource **out) { return give<KeyboardSource, IInputKeyboardSource>(s ? s->m_keyboard : nullptr, out); }
HRESULT input_pretranslate_source(InputSources *s, IInputPreTranslateKeyboardSource **out)
{
    return give<PreTranslateSource, IInputPreTranslateKeyboardSource>(s ? s->m_pretranslate : nullptr, out);
}
HRESULT input_focus_controller(InputSources *s, IInputFocusController **out) { return give<FocusController, IInputFocusController>(s ? s->m_focus : nullptr, out); }
HRESULT input_activation_listener(InputSources *s, IInputActivationListener **out)
{
    return give<ActivationListener, IInputActivationListener>(s ? s->m_activation : nullptr, out);
}

/* ---- the bridge window's messages ------------------------------------------------ */
static bool raise_pointer(InputSources *s, Event<PointerHandler> &ev, POINT client, WPARAM keys, PointerUpdateKind kind,
                          INT32 wheel = 0, bool hwheel = false)
{
    if (ev.Empty()) return false;
    PointerProperties *props = new PointerProperties();
    PtrPoint *pt = new PtrPoint();
    PointerArgs *args = new PointerArgs();
    if (!props || !pt || !args) {
        if (props) props->Release();
        if (pt) pt->Release();
        if (args) args->Release();
        return false;
    }
    float scale = island_scale(s->m_island);
    props->m_keys = keys;
    props->m_kind = kind;
    props->m_wheel = wheel;
    props->m_hwheel = hwheel;
    pt->m_props = props;
    pt->m_pos.X = client.x / scale;
    pt->m_pos.Y = client.y / scale;
    pt->m_time = (UINT64)GetTickCount64() * 1000;      /* (microseconds) */
    pt->m_frame = ++s->m_frame;
    args->m_point = pt;
    args->m_mods = modifiers();
    s->m_pointer->AddRef();
    ev.Raise(static_cast<IInputPointerSource *>(s->m_pointer), static_cast<IPointerEventArgs *>(args));
    bool handled = args->m_handled;
    s->m_pointer->Release();
    args->Release();
    return handled;
}

static bool raise_key(InputSources *s, Event<KeyHandler> &ev, WPARAM vk, LPARAM lp, bool released)
{
    if (ev.Empty()) return false;
    KeyArgs *a = new KeyArgs();
    if (!a) return false;
    a->m_key = (wsys::VirtualKey)vk;
    a->m_status = key_status(lp, released);
    a->m_time = (UINT64)GetTickCount64() * 1000;
    ev.Raise(static_cast<IInputKeyboardSource *>(s->m_keyboard), static_cast<IKeyEventArgs *>(a));
    bool handled = a->m_handled;
    a->Release();
    return handled;
}

bool inputs_message(InputSources *s, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT *result)
{
    POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
    *result = 0;
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!s->m_inside) {
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&t);
            s->m_inside = true;
            raise_pointer(s, s->m_pointer->m_entered, p, wp, PointerUpdateKind_Other);
        }
        return raise_pointer(s, s->m_pointer->m_moved, p, wp, PointerUpdateKind_Other);
    case WM_MOUSELEAVE:
        s->m_inside = false;
        if (!GetCursorPos(&p)) p.x = p.y = 0;
        ScreenToClient(hwnd, &p);
        raise_pointer(s, s->m_pointer->m_exited, p, 0, PointerUpdateKind_Other);
        return true;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN:
    case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MBUTTONDBLCLK: case WM_XBUTTONDBLCLK: {
        PointerUpdateKind kind = msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK ? PointerUpdateKind_LeftButtonPressed
                               : msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK ? PointerUpdateKind_RightButtonPressed
                               : msg == WM_MBUTTONDOWN || msg == WM_MBUTTONDBLCLK ? PointerUpdateKind_MiddleButtonPressed
                               : HIWORD(wp) == XBUTTON1 ? PointerUpdateKind_XButton1Pressed : PointerUpdateKind_XButton2Pressed;
        if (s->m_pointer->m_activation != InputPointerActivationBehavior_NoActivate && GetFocus() != hwnd) SetFocus(hwnd);
        if (GetCapture() != hwnd) SetCapture(hwnd);
        s->m_captured = true;
        raise_pointer(s, s->m_pointer->m_pressed, p, LOWORD(wp), kind);
        return true;
    }
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: case WM_XBUTTONUP: {
        PointerUpdateKind kind = msg == WM_LBUTTONUP ? PointerUpdateKind_LeftButtonReleased
                               : msg == WM_RBUTTONUP ? PointerUpdateKind_RightButtonReleased
                               : msg == WM_MBUTTONUP ? PointerUpdateKind_MiddleButtonReleased
                               : HIWORD(wp) == XBUTTON1 ? PointerUpdateKind_XButton1Released : PointerUpdateKind_XButton2Released;
        raise_pointer(s, s->m_pointer->m_released, p, LOWORD(wp), kind);
        if (!(LOWORD(wp) & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2)) && GetCapture() == hwnd) {
            s->m_captured = false;
            ReleaseCapture();
        }
        return true;
    }
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        ScreenToClient(hwnd, &p);
        raise_pointer(s, s->m_pointer->m_wheel, p, LOWORD(wp), PointerUpdateKind_Other, (short)HIWORD(wp), msg == WM_MOUSEHWHEEL);
        return true;
    case WM_CAPTURECHANGED:
        if (s->m_captured && (HWND)lp != hwnd) {
            s->m_captured = false;
            if (!GetCursorPos(&p)) p.x = p.y = 0;
            ScreenToClient(hwnd, &p);
            raise_pointer(s, s->m_pointer->m_lost, p, 0, PointerUpdateKind_Other);
        }
        return false;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && s->m_pointer->m_cursor) {
            IInputSystemCursor *sys = nullptr;
            if (SUCCEEDED(s->m_pointer->m_cursor->QueryInterface(__uuidof(IInputSystemCursor), (void **)&sys))) {
                SetCursor(static_cast<SystemCursor *>(sys)->load());
                sys->Release();
                *result = TRUE;
                return true;
            }
        }
        return false;
    case WM_KEYDOWN:
        return raise_key(s, s->m_keyboard->m_down, wp, lp, false);
    case WM_KEYUP:
        return raise_key(s, s->m_keyboard->m_up, wp, lp, true);
    case WM_SYSKEYDOWN:
        return raise_key(s, s->m_keyboard->m_sysdown, wp, lp, false);
    case WM_SYSKEYUP:
        return raise_key(s, s->m_keyboard->m_sysup, wp, lp, true);
    case WM_CHAR: {
        if (s->m_keyboard->m_char.Empty()) return false;
        CharArgs *a = new CharArgs();
        if (!a) return false;
        a->m_code = (UINT32)wp;
        a->m_status = key_status(lp, false);
        s->m_keyboard->m_char.Raise(static_cast<IInputKeyboardSource *>(s->m_keyboard), static_cast<ICharacterReceivedEventArgs *>(a));
        bool handled = a->m_handled;
        a->Release();
        return handled;
    }
    case WM_SETFOCUS: case WM_KILLFOCUS: {
        FocusArgs *a = new FocusArgs();
        if (a) {
            (msg == WM_SETFOCUS ? s->m_focus->m_got : s->m_focus->m_lost)
                .Raise(static_cast<IInputFocusController *>(s->m_focus), static_cast<IFocusChangedEventArgs *>(a));
            a->Release();
        }
        s->m_activation->check();
        return false;
    }
    }
    return false;
}

/* ---- InputNonClientPointerSource: a top-level window's title bar regions ------- */
typedef wf::ITypedEventHandler<InputNonClientPointerSource *, NonClientCaptionTappedEventArgs *> CaptionHandler;
typedef wf::ITypedEventHandler<InputNonClientPointerSource *, NonClientPointerEventArgs *> NonClientHandler;
typedef wf::ITypedEventHandler<InputNonClientPointerSource *, NonClientRegionsChangedEventArgs *> RegionsHandler;

struct NonClientSource : Object, Stub<IInputNonClientPointerSource>, Stub<IInputNonClientPointerSource2>
{
    NonClientSource *m_next;
    HWND m_hwnd;
    struct { UINT32 n; ABI::Windows::Graphics::RectInt32 *rects; } m_regions[NonClientRegionKind_Passthrough + 1];
    Event<CaptionHandler> m_tapped;
    Event<NonClientHandler> m_entered, m_exited, m_moved, m_pressed, m_released;
    Event<RegionsHandler> m_changed;
    LIFTED_OBJECT(L"Microsoft.UI.Input.InputNonClientPointerSource")
    LIFTED_QI(NonClientSource, IInputNonClientPointerSource, IInputNonClientPointerSource2)
    ~NonClientSource() { ClearAllRegionRects(); }
    HRESULT STDMETHODCALLTYPE ClearAllRegionRects() override
    {
        for (auto &r : m_regions) {
            if (r.rects) HeapFree(GetProcessHeap(), 0, r.rects);
            r.rects = nullptr;
            r.n = 0;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ClearRegionRects(NonClientRegionKind k) override
    {
        if ((unsigned)k > NonClientRegionKind_Passthrough) return E_INVALIDARG;
        if (m_regions[k].rects) HeapFree(GetProcessHeap(), 0, m_regions[k].rects);
        m_regions[k].rects = nullptr;
        m_regions[k].n = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetRegionRects(NonClientRegionKind k, UINT32 n, ABI::Windows::Graphics::RectInt32 *rects) override
    {
        if (FAILED(ClearRegionRects(k))) return E_INVALIDARG;
        if (!n) return S_OK;
        m_regions[k].rects = (ABI::Windows::Graphics::RectInt32 *)HeapAlloc(GetProcessHeap(), 0, n * sizeof *rects);
        if (!m_regions[k].rects) return E_OUTOFMEMORY;
        memcpy(m_regions[k].rects, rects, n * sizeof *rects);
        m_regions[k].n = n;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRegionRects(NonClientRegionKind k, UINT32 *n, ABI::Windows::Graphics::RectInt32 **result) override
    {
        *n = 0;
        *result = nullptr;
        if ((unsigned)k > NonClientRegionKind_Passthrough) return E_INVALIDARG;
        *result = (ABI::Windows::Graphics::RectInt32 *)CoTaskMemAlloc((m_regions[k].n ? m_regions[k].n : 1) * sizeof **result);
        if (!*result) return E_OUTOFMEMORY;
        memcpy(*result, m_regions[k].rects ? m_regions[k].rects : *result, m_regions[k].n * sizeof **result);
        *n = m_regions[k].n;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_CaptionTapped(CaptionHandler *h, EventRegistrationToken *t) override { return m_tapped.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_CaptionTapped(EventRegistrationToken t) override { return m_tapped.Remove(t); }
#define EVENT(NAME, FIELD)                                                                                                \
    HRESULT STDMETHODCALLTYPE add_##NAME(NonClientHandler *h, EventRegistrationToken *t) override { return FIELD.Add(h, t); } \
    HRESULT STDMETHODCALLTYPE remove_##NAME(EventRegistrationToken t) override { return FIELD.Remove(t); }
    EVENT(PointerEntered, m_entered)
    EVENT(PointerExited, m_exited)
    EVENT(PointerMoved, m_moved)
    EVENT(PointerPressed, m_pressed)
    EVENT(PointerReleased, m_released)
#undef EVENT
    HRESULT STDMETHODCALLTYPE add_RegionsChanged(RegionsHandler *h, EventRegistrationToken *t) override { return m_changed.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_RegionsChanged(EventRegistrationToken t) override { return m_changed.Remove(t); }
};

static SRWLOCK g_nonclient_lock = SRWLOCK_INIT;
static NonClientSource *g_nonclient;        /* (each lives as long as the process: one per window, as the SDK's) */

/* ---- the statics -------------------------------------------------------------------- */
#define ISLAND_STATICS(NAME, CLASS, STATICS, IFACE, GETTER)                                            \
    struct NAME : Object, Stub<STATICS>, IActivationFactory                                            \
    {                                                                                                  \
        LIFTED_OBJECT(CLASS)                                                                           \
        LIFTED_QI(NAME, STATICS, IActivationFactory)                                                   \
        HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable **out) override { *out = nullptr; return E_NOTIMPL; } \
        HRESULT STDMETHODCALLTYPE GetForIsland(ixp::IContentIsland *island, IFACE **result) override  \
        {                                                                                              \
            *result = nullptr;                                                                         \
            if (!island) return E_INVALIDARG;                                                          \
            return GETTER(island_inputs(island), result);                                              \
        }                                                                                              \
    };
ISLAND_STATICS(PointerSourceStatics, L"Microsoft.UI.Input.InputPointerSource", IInputPointerSourceStatics, IInputPointerSource,
               input_pointer_source)
ISLAND_STATICS(PreTranslateStatics, L"Microsoft.UI.Input.InputPreTranslateKeyboardSource", IInputPreTranslateKeyboardSourceStatics,
               IInputPreTranslateKeyboardSource, input_pretranslate_source)
ISLAND_STATICS(FocusControllerStatics, L"Microsoft.UI.Input.InputFocusController", IInputFocusControllerStatics,
               IInputFocusController, input_focus_controller)
#undef ISLAND_STATICS

struct KeyboardSourceStatics : Object, Stub<IInputKeyboardSourceStatics>, Stub<IInputKeyboardSourceStatics2>, IActivationFactory
{
    LIFTED_OBJECT(L"Microsoft.UI.Input.InputKeyboardSource")
    LIFTED_QI(KeyboardSourceStatics, IInputKeyboardSourceStatics, IInputKeyboardSourceStatics2, IActivationFactory)
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable **out) override { *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetForIsland(ixp::IContentIsland *island, IInputKeyboardSource **result) override
    {
        *result = nullptr;
        if (!island) return E_INVALIDARG;
        return input_keyboard_source(island_inputs(island), result);
    }
    HRESULT STDMETHODCALLTYPE GetKeyStateForCurrentThread(wsys::VirtualKey key, ABI::Windows::UI::Core::CoreVirtualKeyStates *result) override
    {
        SHORT s = ::GetKeyState((int)key);
        *result = (ABI::Windows::UI::Core::CoreVirtualKeyStates)((s < 0 ? 1 : 0) | (s & 1 ? 2 : 0));
        return S_OK;
    }
};

struct ActivationListenerStatics : Object, Stub<IInputActivationListenerStatics>, Stub<IInputActivationListenerStatics2>, IActivationFactory
{
    LIFTED_OBJECT(L"Microsoft.UI.Input.InputActivationListener")
    LIFTED_QI(ActivationListenerStatics, IInputActivationListenerStatics, IInputActivationListenerStatics2, IActivationFactory)
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable **out) override { *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetForIsland(ixp::IContentIsland *island, IInputActivationListener **result) override
    {
        *result = nullptr;
        if (!island) return E_INVALIDARG;
        return input_activation_listener(island_inputs(island), result);
    }
};

struct NonClientStatics : Object, Stub<IInputNonClientPointerSourceStatics>, IActivationFactory
{
    LIFTED_OBJECT(L"Microsoft.UI.Input.InputNonClientPointerSource")
    LIFTED_QI(NonClientStatics, IInputNonClientPointerSourceStatics, IActivationFactory)
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable **out) override { *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetForWindowId(mui::WindowId id, IInputNonClientPointerSource **result) override
    {
        *result = nullptr;
        HWND w = (HWND)(ULONG_PTR)id.Value;
        if (!IsWindow(w)) return E_INVALIDARG;
        AcquireSRWLockExclusive(&g_nonclient_lock);
        NonClientSource *s = g_nonclient;
        while (s && s->m_hwnd != w) s = s->m_next;
        if (!s && (s = new NonClientSource()) != nullptr) {
            s->m_hwnd = w;
            s->m_next = g_nonclient;
            g_nonclient = s;
        }
        if (s) s->AddRef();
        ReleaseSRWLockExclusive(&g_nonclient_lock);
        *result = s;
        return s ? S_OK : E_OUTOFMEMORY;
    }
};

struct SystemCursorStatics : Object, Stub<IInputSystemCursorStatics>, IActivationFactory
{
    LIFTED_OBJECT(L"Microsoft.UI.Input.InputSystemCursor")
    LIFTED_QI(SystemCursorStatics, IInputSystemCursorStatics, IActivationFactory)
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable **out) override { *out = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Create(InputSystemCursorShape type, IInputSystemCursor **result) override
    {
        SystemCursor *c = new SystemCursor();
        *result = c;
        if (!c) return E_OUTOFMEMORY;
        c->m_shape = type;
        return S_OK;
    }
};

struct FocusRequest : Object, Stub<IFocusNavigationRequest>
{
    FocusNavigationReason m_reason;
    GUID m_id;
    bool m_has_hint;
    wf::Rect m_hint;
    LIFTED_OBJECT(L"Microsoft.UI.Input.FocusNavigationRequest")
    LIFTED_QI(FocusRequest, IFocusNavigationRequest)
    HRESULT STDMETHODCALLTYPE get_CorrelationId(GUID *v) override { *v = m_id; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Reason(FocusNavigationReason *v) override { *v = m_reason; return S_OK; }
    /* (HintRect: an IReference<Rect>, which this DLL does not box yet: none) */
};

struct FocusRequestStatics : Object, Stub<IFocusNavigationRequestStatics>, IActivationFactory
{
    LIFTED_OBJECT(L"Microsoft.UI.Input.FocusNavigationRequest")
    LIFTED_QI(FocusRequestStatics, IFocusNavigationRequestStatics, IActivationFactory)
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable **out) override { *out = nullptr; return E_NOTIMPL; }
    HRESULT make(FocusNavigationReason reason, const wf::Rect *hint, const GUID *id, IFocusNavigationRequest **result)
    {
        FocusRequest *r = new FocusRequest();
        *result = r;
        if (!r) return E_OUTOFMEMORY;
        r->m_reason = reason;
        if (hint) { r->m_has_hint = true; r->m_hint = *hint; }
        if (id) r->m_id = *id;
        else CoCreateGuid(&r->m_id);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Create(FocusNavigationReason reason, IFocusNavigationRequest **result) override
    {
        return make(reason, nullptr, nullptr, result);
    }
    HRESULT STDMETHODCALLTYPE CreateWithHintRect(FocusNavigationReason reason, wf::Rect hint, IFocusNavigationRequest **result) override
    {
        return make(reason, &hint, nullptr, result);
    }
    HRESULT STDMETHODCALLTYPE CreateWithHintRectAndId(FocusNavigationReason reason, wf::Rect hint, GUID id,
                                                      IFocusNavigationRequest **result) override
    {
        return make(reason, &hint, &id, result);
    }
};

IActivationFactory *input_pointer_source_factory() { return new PointerSourceStatics(); }
IActivationFactory *input_keyboard_source_factory() { return new KeyboardSourceStatics(); }
IActivationFactory *input_pretranslate_factory() { return new PreTranslateStatics(); }
IActivationFactory *input_focus_controller_factory() { return new FocusControllerStatics(); }
IActivationFactory *input_activation_listener_factory() { return new ActivationListenerStatics(); }
IActivationFactory *input_nonclient_factory() { return new NonClientStatics(); }
IActivationFactory *input_system_cursor_factory() { return new SystemCursorStatics(); }
IActivationFactory *focus_navigation_request_factory() { return new FocusRequestStatics(); }
