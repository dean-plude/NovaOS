/* NovaOS: the Windows Runtime's weak references (the Windows SDK's
 * weakreference.h): IWeakReference resolves to the object while it lives,
 * IWeakReferenceSource hands one out. */
#pragma once
#include <inspectable.h>

#ifndef __IWeakReference_INTERFACE_DEFINED__
#define __IWeakReference_INTERFACE_DEFINED__
struct __declspec(uuid("00000037-0000-0000-C000-000000000046")) __declspec(novtable) IWeakReference : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Resolve(REFIID riid, IInspectable **objectReference) = 0;
    template <class Q> HRESULT STDMETHODCALLTYPE Resolve(Q **objectReference)
    {
        return Resolve(__uuidof(Q), (IInspectable **)objectReference);
    }
};
#endif
#ifndef __IWeakReferenceSource_INTERFACE_DEFINED__
#define __IWeakReferenceSource_INTERFACE_DEFINED__
struct __declspec(uuid("00000038-0000-0000-C000-000000000046")) __declspec(novtable) IWeakReferenceSource : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetWeakReference(IWeakReference **weakReference) = 0;
};
#endif
