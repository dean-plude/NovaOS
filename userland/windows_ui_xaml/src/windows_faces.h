// NovaOS: the Windows.UI.Xaml faces of the XAML framework (what
// tools/winrt/winfaces.py's table and thunks, windows_faces.g.cpp, and
// their runtime, windows_faces.cpp, share).
//
// The framework is Microsoft's WinUI 3 source, whose interfaces are the
// lifted Microsoft.UI.Xaml ones.  A face is one interface implemented over
// another: a forward face a Windows.UI.Xaml interface over the framework's
// (what a UWP app or WinUI 2 asks a framework object for), a reverse face a
// framework interface over a Windows one (what the framework calls on an
// app's object: its overrides, an event handler).  A tear-off, NovaTear,
// carries the face's vtable and the object it calls; each thunk converts
// what it is handed and calls the other interface's method by its slot.
#pragma once
#include <windows.h>
#include <objbase.h>
#include <inspectable.h>
#include <activation.h>
#include <winstring.h>
#include <stddef.h>

#define NOVA_FACE_REVERSE  1u   // the impl IID is the framework's
#define NOVA_FACE_OVERRIDE 2u   // a framework overridable interface over an app's overrides
#define NOVA_FACE_DELEGATE 4u   // a delegate: IUnknown and Invoke

struct NovaFace
{
    const GUID *impl;           // the interface the tear-off implements
    const GUID *target;         // the interface it calls most of its methods on
    void *const *vtbl;
    unsigned flags;
    const char *name;
};

// a Windows.UI.Xaml enum or struct boxed: Windows' IReference<T> and the
// framework's, for boxing a Windows box again as the framework's
struct NovaBox
{
    const GUID *windows;
    const GUID *framework;
    unsigned size;
    const char *name;
    const wchar_t *framework_name;  // the framework's box's runtime class name
};

struct NovaTear
{
    void *const *vtbl;
    const NovaFace *face;
    void *obj;                  // the object called: an interface pointer (face->target) or, for an
                                // override face, the app's controlling IInspectable
    LONG ref;
    BOOL direct;                // obj is face->target's interface
};

extern "C" {
extern const NovaFace nova_faces[];
extern const unsigned nova_face_count;
extern const NovaBox nova_boxes[];
extern const unsigned nova_box_count;

HRESULT STDMETHODCALLTYPE nova_tear_qi(NovaTear *t, REFIID iid, void **out);
ULONG STDMETHODCALLTYPE nova_tear_addref(NovaTear *t);
ULONG STDMETHODCALLTYPE nova_tear_release(NovaTear *t);
HRESULT STDMETHODCALLTYPE nova_tear_iids(NovaTear *t, ULONG *count, IID **iids);
HRESULT STDMETHODCALLTYPE nova_tear_name(NovaTear *t, void **name);
HRESULT STDMETHODCALLTYPE nova_tear_trust(NovaTear *t, INT32 *level);

// the interface @iid of the tear-off's object (AddRef'd), for a thunk;
// nova_target_static: of the framework's factory of class @cls (a base
// class's statics)
HRESULT nova_target(NovaTear *t, const GUID *iid, void **p);
HRESULT nova_target_static(NovaTear *t, const wchar_t *cls, const GUID *iid, void **p);
void nova_untarget(NovaTear *t, void *p);

// @a as interface @iid: asked for it, else wrapped in the face that
// implements it over what @a has (null stays null)
HRESULT nova_cross(void *a, const GUID *iid, void **out);
// an out parameter the call @hr gave as @o, as @iid (or, @iid null, as it
// is, a framework box of a Windows.UI.Xaml enum or struct boxed again as
// Windows boxes it) into @a; releases @o
HRESULT nova_out(HRESULT hr, void *o, const GUID *iid, void **a);
// an object going from Windows' types to the framework's: a Windows box
// of a Windows.UI.Xaml enum or struct boxed again as the framework's
HRESULT nova_object_in(void *a, void **out);
HRESULT nova_object_out(HRESULT hr, void *o, void **a);
// an object going from the framework to the app's code: a framework box of
// a Windows.UI.Xaml enum or struct boxed again as Windows boxes it
HRESULT nova_object_to_app(void *a, void **out);
// a TypeName (Windows.UI.Xaml.Interop.TypeName) of a Windows.UI.Xaml type
// renamed as the framework's (@to_framework) or back: in place, the new
// name made into @made (for the caller to delete); nova_typename_out: an
// out parameter's, whose old name it deletes
extern const wchar_t *const nova_xaml_names[];
extern const unsigned nova_xaml_name_count;
void nova_typename(void *typename_, void **made, int to_framework);
void nova_typename_out(void *typename_, int to_framework);
void nova_addref(void *p);
void nova_release(void *p);
// a method with no counterpart: says so once, E_NOTIMPL
HRESULT nova_face_missing(const char *name);
// (an event NovaOS never raises: its handler kept until removed)
HRESULT nova_face_event_kept(IUnknown *handler, INT64 *token);
HRESULT nova_face_event_removed(INT64 token);
}
