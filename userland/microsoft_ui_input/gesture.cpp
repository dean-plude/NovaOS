/*
 * Microsoft.UI.Input's GestureRecognizer: what a pointer's down, move and up
 * points make as gestures, which XAML's input manager asks of one per
 * element it tracks (components/gestures' ElementGestureTracker, with the
 * gesture settings the element's IsTapEnabled, IsRightTapEnabled,
 * IsDoubleTapEnabled and ManipulationMode give it).  The pointer's points
 * come from the island's InputPointerSource (input.cpp), so in the island's
 * DIPs, one mouse pointer:
 *   Tapped          a press and release of the left button that stays
 *                   within the double-click rectangle (SM_CXDOUBLECLK by
 *                   SM_CYDOUBLECLK) and does not start a manipulation;
 *                   TapCount 2 for a second one within the double-click
 *                   time (GetDoubleClickTime) at the same place, when
 *                   DoubleTap is on
 *   RightTapped     a press and release of the right button, likewise
 *   Manipulation*   with ManipulationTranslateX or Y on, a left-button drag
 *                   past the drag rectangle (SM_CXDRAG by SM_CYDRAG)
 *                   starts one (ManipulationStarted), each move updates it
 *                   (the translation on the axes asked for, scale 1, no
 *                   rotation or expansion, velocities from the last move)
 *                   and the release or CompleteGesture completes it
 * Holding, inertia, cross-slide, dragging and mouse-wheel manipulations
 * are not recognized yet (a mouse holds only with HoldWithMouse).
 */
#include "lifted.h"
#ifndef SM_CXDOUBLECLK
#define SM_CXDOUBLECLK 36
#define SM_CYDOUBLECLK 37
#endif

using namespace ixi;
namespace wfc = ABI::Windows::Foundation::Collections;

/* ---- the gestures' event arguments ------------------------------------------ */
struct TappedArgs : Object, Stub<ITappedEventArgs>
{
    wf::Point m_pos;
    UINT32 m_count;
    LIFTED_OBJECT(L"Microsoft.UI.Input.TappedEventArgs")
    LIFTED_QI(TappedArgs, ITappedEventArgs)
    HRESULT STDMETHODCALLTYPE get_PointerDeviceType(PointerDeviceType *v) override { *v = PointerDeviceType_Mouse; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Position(wf::Point *v) override { *v = m_pos; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_TapCount(UINT32 *v) override { *v = m_count; return S_OK; }
};

struct RightTappedArgs : Object, Stub<IRightTappedEventArgs>
{
    wf::Point m_pos;
    LIFTED_OBJECT(L"Microsoft.UI.Input.RightTappedEventArgs")
    LIFTED_QI(RightTappedArgs, IRightTappedEventArgs)
    HRESULT STDMETHODCALLTYPE get_PointerDeviceType(PointerDeviceType *v) override { *v = PointerDeviceType_Mouse; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Position(wf::Point *v) override { *v = m_pos; return S_OK; }
};

/* (one class for the four manipulation events: each answers its own
 * interface's members) */
struct ManipulationArgs : Object, Stub<IManipulationStartedEventArgs>, Stub<IManipulationUpdatedEventArgs>,
                          Stub<IManipulationCompletedEventArgs>
{
    wf::Point m_pos;
    ManipulationDelta m_cumulative, m_delta;
    ManipulationVelocities m_velocities;
    LIFTED_OBJECT(L"Microsoft.UI.Input.ManipulationUpdatedEventArgs")
    LIFTED_QI(ManipulationArgs, IManipulationUpdatedEventArgs, IManipulationStartedEventArgs, IManipulationCompletedEventArgs)
    HRESULT STDMETHODCALLTYPE get_PointerDeviceType(PointerDeviceType *v) override { *v = PointerDeviceType_Mouse; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Position(wf::Point *v) override { *v = m_pos; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Cumulative(ManipulationDelta *v) override { *v = m_cumulative; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Delta(ManipulationDelta *v) override { *v = m_delta; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Velocities(ManipulationVelocities *v) override { *v = m_velocities; return S_OK; }
};

typedef wf::ITypedEventHandler<GestureRecognizer *, TappedEventArgs *> TappedHandler;
typedef wf::ITypedEventHandler<GestureRecognizer *, RightTappedEventArgs *> RightTappedHandler;
typedef wf::ITypedEventHandler<GestureRecognizer *, ManipulationStartedEventArgs *> StartedHandler;
typedef wf::ITypedEventHandler<GestureRecognizer *, ManipulationUpdatedEventArgs *> UpdatedHandler;
typedef wf::ITypedEventHandler<GestureRecognizer *, ManipulationCompletedEventArgs *> CompletedHandler;

/* ---- GestureRecognizer ------------------------------------------------------ */
struct Recognizer : Object, Stub<IGestureRecognizer>
{
    GestureSettings m_settings = GestureSettings_None;
    FLOAT m_pivot_radius;
    ::boolean m_feedback = true, m_auto_inertia = true, m_manipulation_exact, m_cross_exact, m_cross_horizontal;
    FLOAT m_inertia[6];     /* (the inertia settings, kept: expansion and its deceleration, rotation angle and its
                             * deceleration, translation displacement and its deceleration) */

    /* the pointer's press: which button, where and when */
    enum { NONE, LEFT, RIGHT } m_down = NONE;
    wf::Point m_start, m_last;
    UINT64 m_last_time;
    bool m_manipulating, m_moved_out;
    /* the last tap, for a double tap */
    bool m_tapped;
    wf::Point m_tap_pos;
    UINT64 m_tap_time;

    Event<TappedHandler> m_tap;
    Event<RightTappedHandler> m_right_tap;
    Event<StartedHandler> m_started;
    Event<UpdatedHandler> m_updated;
    Event<CompletedHandler> m_completed;

    LIFTED_OBJECT(L"Microsoft.UI.Input.GestureRecognizer")
    LIFTED_QI(Recognizer, IGestureRecognizer)

    static bool within(wf::Point a, wf::Point b, int cx, int cy)
    {
        float dx = a.X - b.X, dy = a.Y - b.Y;
        return (dx < 0 ? -dx : dx) * 2 <= GetSystemMetrics(cx) && (dy < 0 ? -dy : dy) * 2 <= GetSystemMetrics(cy);
    }
    bool translates() const { return (m_settings & (GestureSettings_ManipulationTranslateX | GestureSettings_ManipulationTranslateY)) != 0; }

    ManipulationArgs *args(wf::Point pos)
    {
        ManipulationArgs *a = new ManipulationArgs();
        if (!a) return nullptr;
        a->m_pos = pos;
        a->m_cumulative.Scale = a->m_delta.Scale = 1;
        if (m_settings & GestureSettings_ManipulationTranslateX) a->m_cumulative.Translation.X = pos.X - m_start.X;
        if (m_settings & GestureSettings_ManipulationTranslateY) a->m_cumulative.Translation.Y = pos.Y - m_start.Y;
        return a;
    }
    void complete(wf::Point pos)
    {
        if (!m_manipulating) return;
        m_manipulating = false;
        ManipulationArgs *a = args(pos);
        if (!a) return;
        m_completed.Raise(static_cast<IGestureRecognizer *>(this), static_cast<IManipulationCompletedEventArgs *>(a));
        a->Release();
    }
    void moved(IPointerPoint *point)
    {
        wf::Point pos = {};
        UINT64 time = 0;
        point->get_Position(&pos);
        point->get_Timestamp(&time);
        if (m_down == NONE) return;
        if (!within(pos, m_start, SM_CXDOUBLECLK, SM_CYDOUBLECLK)) m_moved_out = true;
        if (m_down == LEFT && translates()) {
            if (!m_manipulating && !within(pos, m_start, SM_CXDRAG, SM_CYDRAG)) {
                m_manipulating = true;
                ManipulationArgs *a = args(m_start);
                if (a) {
                    a->m_cumulative.Translation = {};
                    m_started.Raise(static_cast<IGestureRecognizer *>(this), static_cast<IManipulationStartedEventArgs *>(a));
                    a->Release();
                }
            }
            if (m_manipulating) {
                ManipulationArgs *a = args(pos);
                if (a) {
                    if (m_settings & GestureSettings_ManipulationTranslateX) a->m_delta.Translation.X = pos.X - m_last.X;
                    if (m_settings & GestureSettings_ManipulationTranslateY) a->m_delta.Translation.Y = pos.Y - m_last.Y;
                    float ms = time > m_last_time ? (float)(time - m_last_time) / 1000.0f : 0;
                    if (ms > 0) {
                        a->m_velocities.Linear.X = a->m_delta.Translation.X / ms;
                        a->m_velocities.Linear.Y = a->m_delta.Translation.Y / ms;
                    }
                    m_updated.Raise(static_cast<IGestureRecognizer *>(this), static_cast<IManipulationUpdatedEventArgs *>(a));
                    a->Release();
                }
            }
        }
        m_last = pos;
        m_last_time = time;
    }

    HRESULT STDMETHODCALLTYPE get_GestureSettings(GestureSettings *v) override { *v = m_settings; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_GestureSettings(GestureSettings v) override { m_settings = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsActive(::boolean *v) override { *v = m_down != NONE || m_manipulating; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsInertial(::boolean *v) override { *v = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_PivotRadius(FLOAT *v) override { *v = m_pivot_radius; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_PivotRadius(FLOAT v) override { m_pivot_radius = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ShowGestureFeedback(::boolean *v) override { *v = m_feedback; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_ShowGestureFeedback(::boolean v) override { m_feedback = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_AutoProcessInertia(::boolean *v) override { *v = m_auto_inertia; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_AutoProcessInertia(::boolean v) override { m_auto_inertia = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_ManipulationExact(::boolean *v) override { *v = m_manipulation_exact; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_ManipulationExact(::boolean v) override { m_manipulation_exact = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CrossSlideExact(::boolean *v) override { *v = m_cross_exact; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_CrossSlideExact(::boolean v) override { m_cross_exact = v; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_CrossSlideHorizontally(::boolean *v) override { *v = m_cross_horizontal; return S_OK; }
    HRESULT STDMETHODCALLTYPE put_CrossSlideHorizontally(::boolean v) override { m_cross_horizontal = v; return S_OK; }
#define INERTIA(NAME, I)                                                                         \
    HRESULT STDMETHODCALLTYPE get_##NAME(FLOAT *v) override { *v = m_inertia[I]; return S_OK; } \
    HRESULT STDMETHODCALLTYPE put_##NAME(FLOAT v) override { m_inertia[I] = v; return S_OK; }
    INERTIA(InertiaExpansion, 0)
    INERTIA(InertiaExpansionDeceleration, 1)
    INERTIA(InertiaRotationAngle, 2)
    INERTIA(InertiaRotationDeceleration, 3)
    INERTIA(InertiaTranslationDisplacement, 4)
    INERTIA(InertiaTranslationDeceleration, 5)
#undef INERTIA

    HRESULT STDMETHODCALLTYPE CanBeDoubleTap(IPointerPoint *point, ::boolean *result) override
    {
        wf::Point pos = {};
        UINT64 time = 0;
        if (!point || !result) return E_POINTER;
        point->get_Position(&pos);
        point->get_Timestamp(&time);
        *result = (m_settings & GestureSettings_DoubleTap) && m_tapped && within(pos, m_tap_pos, SM_CXDOUBLECLK, SM_CYDOUBLECLK) &&
                  time - m_tap_time <= (UINT64)GetDoubleClickTime() * 1000;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ProcessDownEvent(IPointerPoint *point) override
    {
        if (!point) return E_POINTER;
        IPointerPointProperties *props = nullptr;
        ::boolean left = false, right = false;
        if (SUCCEEDED(point->get_Properties(&props)) && props) {
            props->get_IsLeftButtonPressed(&left);
            props->get_IsRightButtonPressed(&right);
            props->Release();
        }
        if (m_down != NONE) return S_OK;            /* (a second button while one is down: the first's gesture) */
        m_down = left ? LEFT : right ? RIGHT : NONE;
        point->get_Position(&m_start);
        point->get_Timestamp(&m_last_time);
        m_last = m_start;
        m_moved_out = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ProcessMoveEvents(wfc::IVector<PointerPoint *> *points) override
    {
        if (!points) return E_POINTER;
        UINT32 n = 0;
        points->get_Size(&n);
        for (UINT32 i = 0; i < n; i++) {
            IPointerPoint *p = nullptr;
            if (SUCCEEDED(points->GetAt(i, &p)) && p) {
                moved(p);
                p->Release();
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ProcessUpEvent(IPointerPoint *point) override
    {
        if (!point) return E_POINTER;
        if (m_down == NONE) return S_OK;
        moved(point);
        wf::Point pos = m_last;
        UINT64 time = m_last_time;
        bool was_manipulating = m_manipulating;
        int button = m_down;
        m_down = NONE;
        complete(pos);
        if (was_manipulating || m_moved_out) {
            m_tapped = false;
            return S_OK;
        }
        if (button == LEFT && (m_settings & (GestureSettings_Tap | GestureSettings_DoubleTap))) {
            UINT32 count = 1;
            if ((m_settings & GestureSettings_DoubleTap) && m_tapped && within(pos, m_tap_pos, SM_CXDOUBLECLK, SM_CYDOUBLECLK) &&
                time - m_tap_time <= (UINT64)GetDoubleClickTime() * 1000)
                count = 2;
            m_tapped = count == 1;
            m_tap_pos = pos;
            m_tap_time = time;
            if (count == 2 || (m_settings & GestureSettings_Tap)) {
                TappedArgs *a = new TappedArgs();
                if (!a) return E_OUTOFMEMORY;
                a->m_pos = pos;
                a->m_count = count;
                m_tap.Raise(static_cast<IGestureRecognizer *>(this), static_cast<ITappedEventArgs *>(a));
                a->Release();
            }
        } else if (button == RIGHT && (m_settings & GestureSettings_RightTap)) {
            RightTappedArgs *a = new RightTappedArgs();
            if (!a) return E_OUTOFMEMORY;
            a->m_pos = pos;
            m_right_tap.Raise(static_cast<IGestureRecognizer *>(this), static_cast<IRightTappedEventArgs *>(a));
            a->Release();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ProcessInertia() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE CompleteGesture() override
    {
        m_down = NONE;
        complete(m_last);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE add_Tapped(TappedHandler *h, EventRegistrationToken *t) override { return m_tap.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_Tapped(EventRegistrationToken t) override { return m_tap.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_RightTapped(RightTappedHandler *h, EventRegistrationToken *t) override { return m_right_tap.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_RightTapped(EventRegistrationToken t) override { return m_right_tap.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_ManipulationStarted(StartedHandler *h, EventRegistrationToken *t) override { return m_started.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_ManipulationStarted(EventRegistrationToken t) override { return m_started.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_ManipulationUpdated(UpdatedHandler *h, EventRegistrationToken *t) override { return m_updated.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_ManipulationUpdated(EventRegistrationToken t) override { return m_updated.Remove(t); }
    HRESULT STDMETHODCALLTYPE add_ManipulationCompleted(CompletedHandler *h, EventRegistrationToken *t) override { return m_completed.Add(h, t); }
    HRESULT STDMETHODCALLTYPE remove_ManipulationCompleted(EventRegistrationToken t) override { return m_completed.Remove(t); }
};

struct RecognizerFactory : Object, IActivationFactory
{
    LIFTED_OBJECT(L"Microsoft.UI.Input.GestureRecognizer")
    LIFTED_QI(RecognizerFactory, IActivationFactory)
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable **out) override
    {
        if (!out) return E_POINTER;
        Recognizer *r = new Recognizer();
        *out = static_cast<IGestureRecognizer *>(r);
        return r ? S_OK : E_OUTOFMEMORY;
    }
};

IActivationFactory *gesture_recognizer_factory() { return new RecognizerFactory(); }
