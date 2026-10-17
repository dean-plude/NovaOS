/* NovaOS's Windows Runtime C++ Template Library (WRL): the interface of the
 * Windows SDK's <wrl/client.h>, written for NovaOS (Microsoft's XAML sources
 * in third_party/microsoft-ui-xaml are built on it).  ComPtr, the reference
 * ComPtr's operator& gives, WeakRef and AsWeak. */
#pragma once
#include <unknwn.h>
#include <inspectable.h>
#include <weakreference.h>
#include <roapi.h>
#include <wrl/def.h>
#include <wrl/internal.h>

namespace Microsoft { namespace WRL {

template <class T> class ComPtr;
class WeakRef;

namespace Details {

/* what ComPtr's operator& gives: converts to T**, void**, IUnknown**... */
template <class T>
class ComPtrRef
{
public:
    typedef typename T::InterfaceType InterfaceType;
    ComPtrRef(T *ptr) throw() : ptr_(ptr) {}
    operator InterfaceType **() throw() { return ptr_->ReleaseAndGetAddressOf(); }
    operator void **() const throw() { return reinterpret_cast<void **>(ptr_->ReleaseAndGetAddressOf()); }
    template <class U = InterfaceType,
              typename = typename EnableIf<!IsSame<U, IInspectable>::value && __is_base_of(IInspectable, U), void>::type>
    operator IInspectable **() const throw() { return reinterpret_cast<IInspectable **>(ptr_->ReleaseAndGetAddressOf()); }
    template <class U = InterfaceType, typename = typename EnableIf<!IsSame<U, IUnknown>::value, void>::type>
    operator IUnknown **() const throw() { return reinterpret_cast<IUnknown **>(ptr_->ReleaseAndGetAddressOf()); }
    operator T *() throw() { *ptr_ = nullptr; return ptr_; }
    InterfaceType *operator*() throw() { return ptr_->Get(); }
    InterfaceType *const *GetAddressOf() const throw() { return ptr_->GetAddressOf(); }
    InterfaceType **ReleaseAndGetAddressOf() throw() { return ptr_->ReleaseAndGetAddressOf(); }
protected:
    T *ptr_;
};

template <class T>
class ComPtrRefBase : public ComPtrRef<T>
{
public:
    using ComPtrRef<T>::ComPtrRef;
};

} // namespace Details

template <class T>
class ComPtr
{
public:
    typedef T InterfaceType;

protected:
    InterfaceType *ptr_;
    template <class U> friend class ComPtr;

    void InternalAddRef() const throw() { if (ptr_) ptr_->AddRef(); }
    unsigned long InternalRelease() throw()
    {
        unsigned long r = 0;
        T *t = ptr_;
        if (t) { ptr_ = nullptr; r = t->Release(); }
        return r;
    }

public:
    ComPtr() throw() : ptr_(nullptr) {}
    ComPtr(decltype(__nullptr)) throw() : ptr_(nullptr) {}
    template <class U> ComPtr(U *other) throw() : ptr_(other) { InternalAddRef(); }
    ComPtr(const ComPtr &other) throw() : ptr_(other.ptr_) { InternalAddRef(); }
    template <class U, typename = typename Details::EnableIf<__is_convertible_to(U *, T *), void>::type>
    ComPtr(const ComPtr<U> &other) throw() : ptr_(other.ptr_) { InternalAddRef(); }
    ComPtr(ComPtr &&other) throw() : ptr_(nullptr) { if (this != reinterpret_cast<ComPtr *>(&reinterpret_cast<unsigned char &>(other))) Swap(other); }
    template <class U, typename = typename Details::EnableIf<__is_convertible_to(U *, T *), void>::type>
    ComPtr(ComPtr<U> &&other) throw() : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    ~ComPtr() throw() { InternalRelease(); }

    ComPtr &operator=(decltype(__nullptr)) throw() { InternalRelease(); return *this; }
    ComPtr &operator=(T *other) throw()
    {
        if (ptr_ != other) ComPtr(other).Swap(*this);
        return *this;
    }
    template <class U> ComPtr &operator=(U *other) throw() { ComPtr(other).Swap(*this); return *this; }
    ComPtr &operator=(const ComPtr &other) throw()
    {
        if (ptr_ != other.ptr_) ComPtr(other).Swap(*this);
        return *this;
    }
    template <class U> ComPtr &operator=(const ComPtr<U> &other) throw() { ComPtr(other).Swap(*this); return *this; }
    ComPtr &operator=(ComPtr &&other) throw() { ComPtr(static_cast<ComPtr &&>(other)).Swap(*this); return *this; }
    template <class U> ComPtr &operator=(ComPtr<U> &&other) throw() { ComPtr(static_cast<ComPtr<U> &&>(other)).Swap(*this); return *this; }

    void Swap(ComPtr &&r) throw() { T *tmp = ptr_; ptr_ = r.ptr_; r.ptr_ = tmp; }
    void Swap(ComPtr &r) throw() { T *tmp = ptr_; ptr_ = r.ptr_; r.ptr_ = tmp; }

    operator Details::BoolType() const throw() { return Get() != nullptr ? &Details::BoolStruct::Member : nullptr; }
    T *Get() const throw() { return ptr_; }
    InterfaceType *operator->() const throw() { return ptr_; }
    Details::ComPtrRef<ComPtr<T>> operator&() throw() { return Details::ComPtrRef<ComPtr<T>>(this); }
    const Details::ComPtrRef<const ComPtr<T>> operator&() const throw() { return Details::ComPtrRef<const ComPtr<T>>(this); }
    T *const *GetAddressOf() const throw() { return &ptr_; }
    T **GetAddressOf() throw() { return &ptr_; }
    T **ReleaseAndGetAddressOf() throw() { InternalRelease(); return &ptr_; }
    T *Detach() throw() { T *p = ptr_; ptr_ = nullptr; return p; }
    void Attach(InterfaceType *other) throw()
    {
        if (ptr_ != nullptr) ptr_->Release();
        ptr_ = other;
    }
    unsigned long Reset() { return InternalRelease(); }

    HRESULT CopyTo(InterfaceType **ptr) const throw() { InternalAddRef(); *ptr = ptr_; return S_OK; }
    HRESULT CopyTo(REFIID riid, void **ptr) const throw() { return ptr_->QueryInterface(riid, ptr); }
    template <class U> HRESULT CopyTo(U **ptr) const throw() { return ptr_->QueryInterface(__uuidof(U), reinterpret_cast<void **>(ptr)); }

    template <class U> HRESULT As(Details::ComPtrRef<ComPtr<U>> p) const throw()
    {
        return ptr_->QueryInterface(__uuidof(U), p);
    }
    template <class U> HRESULT As(ComPtr<U> *p) const throw()
    {
        return ptr_->QueryInterface(__uuidof(U), reinterpret_cast<void **>(p->ReleaseAndGetAddressOf()));
    }
    HRESULT AsIID(REFIID riid, ComPtr<IUnknown> *p) const throw()
    {
        return ptr_->QueryInterface(riid, reinterpret_cast<void **>(p->ReleaseAndGetAddressOf()));
    }
    HRESULT AsWeak(WeakRef *pWeakRef) const throw();
};

template <class T, class U> bool operator==(const ComPtr<T> &a, const ComPtr<U> &b) throw()
{
    return static_cast<IUnknown *>(a.Get()) == static_cast<IUnknown *>(b.Get());
}
template <class T> bool operator==(const ComPtr<T> &a, decltype(__nullptr)) throw() { return a.Get() == nullptr; }
template <class T> bool operator==(decltype(__nullptr), const ComPtr<T> &a) throw() { return a.Get() == nullptr; }
template <class T, class U> bool operator!=(const ComPtr<T> &a, const ComPtr<U> &b) throw() { return !(a == b); }
template <class T> bool operator!=(const ComPtr<T> &a, decltype(__nullptr)) throw() { return a.Get() != nullptr; }
template <class T> bool operator!=(decltype(__nullptr), const ComPtr<T> &a) throw() { return a.Get() != nullptr; }
template <class T, class U> bool operator<(const ComPtr<T> &a, const ComPtr<U> &b) throw()
{
    return static_cast<IUnknown *>(a.Get()) < static_cast<IUnknown *>(b.Get());
}
template <class T, class U> bool operator==(const Details::ComPtrRef<ComPtr<T>> &a, const Details::ComPtrRef<ComPtr<U>> &b) throw()
{
    return a.GetAddressOf() == b.GetAddressOf();
}

/* a weak reference to an object that gives out IWeakReferenceSource */
class WeakRef : public ComPtr<IWeakReference>
{
protected:
    struct ComPtrRef_WeakRef
    {
        ComPtrRef_WeakRef(WeakRef *p) : p_(p) {}
        operator IWeakReference **() { return p_->ReleaseAndGetAddressOf(); }
        operator void **() { return reinterpret_cast<void **>(p_->ReleaseAndGetAddressOf()); }
        operator WeakRef *() { *p_ = nullptr; return p_; }
        WeakRef *p_;
    };

public:
    WeakRef() throw() {}
    WeakRef(decltype(__nullptr)) throw() {}
    WeakRef(IWeakReference *ptr) throw() : ComPtr<IWeakReference>(ptr) {}
    WeakRef(const ComPtr<IWeakReference> &ptr) throw() : ComPtr<IWeakReference>(ptr) {}
    WeakRef(const WeakRef &ptr) throw() : ComPtr<IWeakReference>(ptr) {}
    WeakRef(WeakRef &&ptr) throw() : ComPtr<IWeakReference>(static_cast<ComPtr<IWeakReference> &&>(ptr)) {}
    WeakRef &operator=(const WeakRef &o) throw() { ComPtr<IWeakReference>::operator=(o); return *this; }
    WeakRef &operator=(WeakRef &&o) throw() { ComPtr<IWeakReference>::operator=(static_cast<ComPtr<IWeakReference> &&>(o)); return *this; }
    WeakRef &operator=(decltype(__nullptr)) throw() { ComPtr<IWeakReference>::operator=(nullptr); return *this; }

    ComPtrRef_WeakRef operator&() throw() { return ComPtrRef_WeakRef(this); }

    HRESULT As(REFIID riid, IInspectable **ptr) const throw() { return AsIID(riid, ptr); }
    HRESULT AsIID(REFIID riid, IInspectable **ptr) const throw()
    {
        *ptr = nullptr;
        if (!ptr_) return E_POINTER;
        return ptr_->Resolve(riid, ptr);
    }
    HRESULT AsIID(REFIID riid, ComPtr<IInspectable> *ptr) const throw() { return AsIID(riid, ptr->ReleaseAndGetAddressOf()); }
    template <class U> HRESULT As(Details::ComPtrRef<ComPtr<U>> p) const throw()
    {
        return AsIID(__uuidof(U), reinterpret_cast<IInspectable **>(static_cast<U **>(p)));
    }
    template <class U> HRESULT As(ComPtr<U> *p) const throw()
    {
        return AsIID(__uuidof(U), reinterpret_cast<IInspectable **>(p->ReleaseAndGetAddressOf()));
    }
    template <class U> HRESULT CopyTo(U **ptr) const throw()
    {
        return AsIID(__uuidof(U), reinterpret_cast<IInspectable **>(ptr));
    }
    HRESULT CopyTo(IWeakReference **ptr) const throw() { return ComPtr<IWeakReference>::CopyTo(ptr); }
    HRESULT CopyTo(REFIID riid, IInspectable **ptr) const throw() { return AsIID(riid, ptr); }
};

inline HRESULT AsWeak(IUnknown *unk, WeakRef *pWeakRef) throw()
{
    ComPtr<IWeakReferenceSource> src;
    *pWeakRef = nullptr;
    HRESULT hr = unk->QueryInterface(__uuidof(IWeakReferenceSource), reinterpret_cast<void **>(src.GetAddressOf()));
    if (FAILED(hr)) return hr;
    ComPtr<IWeakReference> w;
    hr = src->GetWeakReference(w.GetAddressOf());
    if (SUCCEEDED(hr)) *pWeakRef = WeakRef(w);
    return hr;
}

template <class T> HRESULT ComPtr<T>::AsWeak(WeakRef *pWeakRef) const throw()
{
    return ::Microsoft::WRL::AsWeak(ptr_, pWeakRef);
}

}} // namespace Microsoft::WRL

/* <wrl/client.h>'s overloads of roapi.h's C++ helpers that take &comPtr */
namespace ABI { namespace Windows { namespace Foundation {
template <class T> inline HRESULT GetActivationFactory(HSTRING activatableClassId, ::Microsoft::WRL::Details::ComPtrRef<T> factory) throw()
{
    return RoGetActivationFactory(activatableClassId, __uuidof(typename T::InterfaceType),
                                  reinterpret_cast<void **>(factory.ReleaseAndGetAddressOf()));
}
template <class T> inline HRESULT ActivateInstance(HSTRING activatableClassId, ::Microsoft::WRL::Details::ComPtrRef<T> instance) throw()
{
    return ActivateInstance(activatableClassId, instance.ReleaseAndGetAddressOf());
}
}}}

/* IID_PPV_ARGS(&comPtr): the ComPtr's address, emptied, as the void ** */
template <class T> inline void **IID_PPV_ARGS_Helper(::Microsoft::WRL::Details::ComPtrRef<T> pp) throw()
{
    return pp;
}
