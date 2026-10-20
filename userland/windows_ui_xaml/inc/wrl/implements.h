/* NovaOS's WRL (see wrl/client.h): <wrl/implements.h>.  RuntimeClass and
 * Implements make a COM or Windows Runtime object from a list of
 * interfaces (and the WRL markers CloakedIid, ChainInterfaces, MixIn,
 * FtmBase, ComposableBase); Make and MakeAndInitialize create one.  The
 * interface follows the Windows SDK's; the inside is NovaOS's own: each list
 * entry answers QueryInterface for its interfaces, the reference count is
 * one word, and a weak reference (IWeakReferenceSource, for WinRt classes
 * unless InhibitWeakReference) is a small object made on first request. */
#pragma once
#include <new>
#include <objidl.h>
#include <wrl/def.h>
#include <wrl/internal.h>
#include <wrl/client.h>
#include <roerrorapi.h>

namespace Microsoft { namespace WRL {

template <unsigned int flags> struct RuntimeClassFlags { static const unsigned int value = flags; };
typedef RuntimeClassFlags<WinRt | InhibitWeakReference> InhibitWeakReferencePolicy;

namespace Details {
struct ImplementsMarker {};
struct ChainInterfacesMarker {};
struct MixInMarker {};
struct ComposableBaseMarker {};
template <class T> struct IsFlags { static const bool value = false; };
template <unsigned int f> struct IsFlags<RuntimeClassFlags<f>> { static const bool value = true; };
template <unsigned int N> struct NilAt {};
template <bool c, class A, class B> struct Conditional { typedef A type; };
template <class A, class B> struct Conditional<false, A, B> { typedef B type; };
} // namespace Details

/* an interface QueryInterface answers but GetIids does not list */
template <class I> struct CloakedIid : public I {};

/* Derived implements MixInType (an Implements class it derives from as well) */
template <class Derived, class MixInType, bool hasImplements = true> struct MixIn : Details::MixInMarker {};

/* I0, which derives from I1, which derives from I2...: one vtable that
 * QueryInterface hands out for each of them */
template <class I0, class I1, class I2 = Details::Nil, class I3 = Details::Nil, class I4 = Details::Nil,
          class I5 = Details::Nil, class I6 = Details::Nil, class I7 = Details::Nil, class I8 = Details::Nil,
          class I9 = Details::Nil>
struct ChainInterfaces : public I0, Details::ChainInterfacesMarker
{
    template <class C> static bool QueryChain(C *c, REFIID riid, void **ppv)
    {
        return Is<I0>(c, riid, ppv) || Is<I1>(c, riid, ppv) || Is<I2>(c, riid, ppv) || Is<I3>(c, riid, ppv) ||
               Is<I4>(c, riid, ppv) || Is<I5>(c, riid, ppv) || Is<I6>(c, riid, ppv) || Is<I7>(c, riid, ppv) ||
               Is<I8>(c, riid, ppv) || Is<I9>(c, riid, ppv);
    }
    static const unsigned int IidCount = 10;
    static void FillIids(IID *&p)
    {
        Fill<I0>(p); Fill<I1>(p); Fill<I2>(p); Fill<I3>(p); Fill<I4>(p);
        Fill<I5>(p); Fill<I6>(p); Fill<I7>(p); Fill<I8>(p); Fill<I9>(p);
    }
private:
    template <class I, class C> static bool Is(C *c, REFIID riid, void **ppv)
    {
        if constexpr (Details::IsSame<I, Details::Nil>::value) {
            return false;
        } else {
            if (!InlineIsEqualGUID(riid, __uuidof(I))) return false;
            *ppv = static_cast<I *>(static_cast<I0 *>(c));
            return true;
        }
    }
    template <class I> static void Fill(IID *&p)
    {
        if constexpr (!Details::IsSame<I, Details::Nil>::value)
            if constexpr (__is_base_of(IInspectable, I) && !Details::IsSame<I, IInspectable>::value)
                *p++ = __uuidof(I);
    }
};

namespace Details {

/* how one entry of an interface list takes part in QueryInterface and GetIids */
template <class T, class = void> struct Entry
{
    template <class C> static bool Query(C *c, REFIID riid, void **ppv)
    {
        if (!InlineIsEqualGUID(riid, __uuidof(T))) return false;
        *ppv = static_cast<T *>(c);
        return true;
    }
    static const unsigned int IidCount = 1;
    static void FillIids(IID *&p)
    {
        if constexpr (__is_base_of(IInspectable, T) && !IsSame<T, IInspectable>::value)
            *p++ = __uuidof(T);
    }
    template <class C> static IUnknown *Identity(C *c) { return static_cast<T *>(c); }
};
template <> struct Entry<Nil, void>
{
    template <class C> static bool Query(C *, REFIID, void **) { return false; }
    static const unsigned int IidCount = 0;
    static void FillIids(IID *&) {}
    template <class C> static IUnknown *Identity(C *) { return nullptr; }
};
template <unsigned int N> struct Entry<NilAt<N>, void> : Entry<Nil, void> {};
template <unsigned int f> struct Entry<RuntimeClassFlags<f>, void> : Entry<Nil, void> {};
template <class I> struct Entry<CloakedIid<I>, void>
{
    template <class C> static bool Query(C *c, REFIID riid, void **ppv)
    {
        if (!InlineIsEqualGUID(riid, __uuidof(I))) return false;
        *ppv = static_cast<I *>(static_cast<CloakedIid<I> *>(c));
        return true;
    }
    static const unsigned int IidCount = 0;
    static void FillIids(IID *&) {}
    template <class C> static IUnknown *Identity(C *c) { return static_cast<I *>(static_cast<CloakedIid<I> *>(c)); }
};
template <class T> struct Entry<T, typename EnableIf<__is_base_of(ChainInterfacesMarker, T)>::type>
{
    template <class C> static bool Query(C *c, REFIID riid, void **ppv) { return T::QueryChain(static_cast<T *>(c), riid, ppv); }
    static const unsigned int IidCount = T::IidCount;
    static void FillIids(IID *&p) { T::FillIids(p); }
    template <class C> static IUnknown *Identity(C *c) { return static_cast<T *>(c); }
};
template <class T> struct Entry<T, typename EnableIf<__is_base_of(ImplementsMarker, T)>::type>
{
    template <class C> static bool Query(C *c, REFIID riid, void **ppv) { return static_cast<T *>(c)->CanCastTo(riid, ppv); }
    static const unsigned int IidCount = T::IidCount;
    static void FillIids(IID *&p) { T::FillIids(p); }
    template <class C> static IUnknown *Identity(C *c) { return static_cast<T *>(c)->CastToUnknown(); }
};
template <class Derived, class MixInType, bool h> struct Entry<MixIn<Derived, MixInType, h>, void>
{
    template <class C> static bool Query(C *c, REFIID riid, void **ppv)
    {
        return static_cast<MixInType *>(static_cast<Derived *>(c))->CanCastTo(riid, ppv);
    }
    static const unsigned int IidCount = MixInType::IidCount;
    static void FillIids(IID *&p) { MixInType::FillIids(p); }
    template <class C> static IUnknown *Identity(C *) { return nullptr; }
};
template <class T> struct Entry<T, typename EnableIf<__is_base_of(ComposableBaseMarker, T)>::type> : Entry<Nil, void> {};

/* the entries of a list, in order */
template <class... Ts> struct EntryList;
template <> struct EntryList<>
{
    template <class C> static bool Query(C *, REFIID, void **) { return false; }
    static const unsigned int IidCount = 0;
    static void FillIids(IID *&) {}
    template <class C> static IUnknown *Identity(C *) { return nullptr; }
};
template <class T, class... Ts> struct EntryList<T, Ts...>
{
    template <class C> static bool Query(C *c, REFIID riid, void **ppv)
    {
        return Entry<T>::Query(c, riid, ppv) || EntryList<Ts...>::Query(c, riid, ppv);
    }
    static const unsigned int IidCount = Entry<T>::IidCount + EntryList<Ts...>::IidCount;
    static void FillIids(IID *&p)
    {
        Entry<T>::FillIids(p);
        EntryList<Ts...>::FillIids(p);
    }
    template <class C> static IUnknown *Identity(C *c)
    {
        IUnknown *u = Entry<T>::Identity(c);
        return u ? u : EntryList<Ts...>::Identity(c);
    }
};

/* a class deriving from every entry but the markers that carry no base */
template <class T, unsigned int N> struct BaseOf { typedef T type; };
template <unsigned int N> struct BaseOf<Nil, N> { typedef NilAt<N> type; };
template <unsigned int f, unsigned int N> struct BaseOf<RuntimeClassFlags<f>, N> { typedef NilAt<N> type; };
template <class D, class M, bool h, unsigned int N> struct BaseOf<MixIn<D, M, h>, N> { typedef NilAt<N> type; };

template <unsigned int N, class... Ts> struct DeriveAll {};
template <unsigned int N, class T, class... Ts>
struct DeriveAll<N, T, Ts...> : public BaseOf<T, N>::type, public DeriveAll<N + 1, Ts...> {};

template <class... Ts> struct FirstFlags { typedef RuntimeClassFlags<WinRt> type; };
template <unsigned int f, class... Ts> struct FirstFlags<RuntimeClassFlags<f>, Ts...> { typedef RuntimeClassFlags<f> type; };

template <class T> struct HasInspectable { static const bool value = __is_base_of(IInspectable, T); };
template <class... Ts> struct AnyInspectable { static const bool value = false; };
template <class T, class... Ts> struct AnyInspectable<T, Ts...>
{
    static const bool value = HasInspectable<T>::value || AnyInspectable<Ts...>::value;
};

/* the weak reference a RuntimeClass hands out: resolves while the object's
 * count is above zero */
class WeakReferenceImpl final : public IWeakReference
{
public:
    WeakReferenceImpl(IUnknown *target, volatile long *strong) : refs_(1), target_(target), strong_(strong) { ::InitializeSRWLock(&lock_); }
    STDMETHOD(QueryInterface)(REFIID riid, void **ppv) override
    {
        if (InlineIsEqualGUID(riid, __uuidof(IUnknown)) || InlineIsEqualGUID(riid, __uuidof(IWeakReference))) {
            *ppv = static_cast<IWeakReference *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return ::InterlockedIncrement(&refs_); }
    STDMETHOD_(ULONG, Release)() override
    {
        long r = ::InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return r;
    }
    STDMETHOD(Resolve)(REFIID riid, IInspectable **out) override
    {
        *out = nullptr;
        IUnknown *t = nullptr;
        ::AcquireSRWLockShared(&lock_);
        if (target_) {
            long c = *strong_;
            while (c > 0) {
                long seen = ::InterlockedCompareExchange(strong_, c + 1, c);
                if (seen == c) {
                    t = target_;
                    break;
                }
                c = seen;
            }
        }
        ::ReleaseSRWLockShared(&lock_);
        if (!t) return S_OK;
        HRESULT hr = t->QueryInterface(riid, reinterpret_cast<void **>(out));
        t->Release();
        return hr;
    }
    void Clear()
    {
        ::AcquireSRWLockExclusive(&lock_);
        target_ = nullptr;
        ::ReleaseSRWLockExclusive(&lock_);
    }
private:
    volatile long refs_;
    SRWLOCK lock_;
    IUnknown *target_;
    volatile long *strong_;
};

} // namespace Details

/* COM's IUnknown machinery for a list of interfaces, to derive from:
 * RuntimeClass<[flags,] I1, I2, ...> */
template <class... Ts>
class __declspec(novtable) RuntimeClass
    : public Details::DeriveAll<0, Ts...>,
      public Details::Conditional<
          (Details::FirstFlags<Ts...>::type::value & WinRt) && !(Details::FirstFlags<Ts...>::type::value & InhibitWeakReference),
          IWeakReferenceSource, Details::NilAt<1000>>::type
{
public:
    typedef RuntimeClass RuntimeClassT;
    typedef typename Details::FirstFlags<Ts...>::type ClassFlags;
    static const bool HasWeakSource = (ClassFlags::value & WinRt) && !(ClassFlags::value & InhibitWeakReference);
    typedef Details::EntryList<Ts...> Entries;
    static const unsigned int IidCount = Entries::IidCount;

    RuntimeClass() throw() : refcount_(1), weak_(nullptr) {}
    virtual ~RuntimeClass() throw()
    {
        if (weak_) weak_->Release();
    }
    RuntimeClass(const RuntimeClass &) = delete;
    RuntimeClass &operator=(const RuntimeClass &) = delete;

    STDMETHOD(QueryInterface)(REFIID riid, void **ppv) override { return InternalQueryInterface(riid, ppv); }
    STDMETHOD_(ULONG, AddRef)() override { return InternalAddRef(); }
    STDMETHOD_(ULONG, Release)() override { return InternalRelease(); }

    STDMETHOD(GetIids)(ULONG *count, IID **iids)
    {
        *count = 0;
        *iids = nullptr;
        IID tmp[IidCount ? IidCount : 1];
        IID *p = tmp;
        Entries::FillIids(p);
        ULONG n = static_cast<ULONG>(p - tmp);
        if (n) {
            *iids = static_cast<IID *>(::CoTaskMemAlloc(sizeof(IID) * n));
            if (!*iids) return E_OUTOFMEMORY;
            for (ULONG i = 0; i < n; i++) (*iids)[i] = tmp[i];
        }
        *count = n;
        return S_OK;
    }
    STDMETHOD(GetRuntimeClassName)(HSTRING *name)
    {
        *name = nullptr;
        return E_ILLEGAL_METHOD_CALL;
    }
    STDMETHOD(GetTrustLevel)(TrustLevel *level)
    {
        *level = BaseTrust;
        return S_OK;
    }
    STDMETHOD(GetWeakReference)(IWeakReference **out)
    {
        *out = nullptr;
        if constexpr (!HasWeakSource) {
            return E_NOINTERFACE;
        } else {
            Details::WeakReferenceImpl *w = weak_;
            if (!w) {
                w = new (std::nothrow) Details::WeakReferenceImpl(CastToUnknown(), &refcount_);
                if (!w) return E_OUTOFMEMORY;
                Details::WeakReferenceImpl *prev = static_cast<Details::WeakReferenceImpl *>(
                    ::InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(&weak_), w, nullptr));
                if (prev) {
                    w->Release();
                    w = prev;
                }
            }
            w->AddRef();
            *out = w;
            return S_OK;
        }
    }

    IUnknown *CastToUnknown() throw() { return Entries::Identity(this); }
    unsigned long GetRefCount() const throw() { return static_cast<unsigned long>(refcount_); }

protected:
    HRESULT InternalQueryInterface(REFIID riid, void **ppv) throw()
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (InlineIsEqualGUID(riid, __uuidof(IUnknown)) ||
            (Details::AnyInspectable<Ts...>::value && InlineIsEqualGUID(riid, __uuidof(IInspectable)))) {
            *ppv = CastToUnknown();
        } else if (InlineIsEqualGUID(riid, __uuidof(IAgileObject)) && (ClassFlags::value & WinRt) &&
                   !(ClassFlags::value & InhibitFtmBase)) {
            *ppv = CastToUnknown();
        } else if (HasWeakSource && InlineIsEqualGUID(riid, __uuidof(IWeakReferenceSource))) {
            *ppv = WeakSource();
        } else if (!Entries::Query(this, riid, ppv)) {
            return FallbackQuery(riid, ppv);
        }
        static_cast<IUnknown *>(*ppv)->AddRef();
        return S_OK;
    }
    virtual HRESULT FallbackQuery(REFIID, void **ppv) { *ppv = nullptr; return E_NOINTERFACE; }
    unsigned long InternalAddRef() throw() { return static_cast<unsigned long>(::InterlockedIncrement(&refcount_)); }
    unsigned long InternalRelease() throw()
    {
        long r = ::InterlockedDecrement(&refcount_);
        if (r == 0) {
            if (weak_) weak_->Clear();
            delete this;
        }
        return static_cast<unsigned long>(r);
    }
    unsigned long DecrementReference() throw() { return static_cast<unsigned long>(::InterlockedDecrement(&refcount_)); }

private:
    void *WeakSource()
    {
        if constexpr (HasWeakSource)
            return static_cast<IWeakReferenceSource *>(this);
        else
            return nullptr;
    }

    volatile long refcount_;
    Details::WeakReferenceImpl *volatile weak_;
};

/* a part of a class: the interfaces it adds, answered by its CanCastTo.
 * RuntimeClass and MixIn ask it. */
template <class... Ts>
struct __declspec(novtable) Implements : public Details::DeriveAll<0, Ts...>, Details::ImplementsMarker
{
    typedef typename Details::FirstFlags<Ts...>::type ClassFlags;
    typedef Details::EntryList<Ts...> Entries;
    static const unsigned int IidCount = Entries::IidCount;
    typedef Implements ImplementsT;

    bool CanCastTo(REFIID riid, void **ppv) throw() { return Entries::Query(this, riid, ppv); }
    IUnknown *CastToUnknown() throw() { return Entries::Identity(this); }
    static void FillIids(IID *&p) { Entries::FillIids(p); }
};

/* the aggregation a composable class's instance makes with its base */
template <class FactoryInterface = IInspectable>
class ComposableBase : public Details::ComposableBaseMarker
{
public:
    HRESULT SetComposableBasePointers(IInspectable *base, FactoryInterface *baseFactory = nullptr) throw()
    {
        if (composableBase_) return E_UNEXPECTED;
        composableBase_ = base;
        composableBaseFactory_ = baseFactory;
        return S_OK;
    }
    ComPtr<IInspectable> &GetComposableBase() throw() { return composableBase_; }
    HRESULT CreateInstance(REFIID riid, void **ppv) throw()
    {
        return composableBase_ ? composableBase_->QueryInterface(riid, ppv) : E_NOINTERFACE;
    }
protected:
    ComPtr<IInspectable> composableBase_;
    ComPtr<FactoryInterface> composableBaseFactory_;
};

namespace Details {
template <class T> struct MakeAllocator
{
    MakeAllocator() throw() : buffer_(nullptr) {}
    ~MakeAllocator() throw()
    {
        if (buffer_) ::operator delete(buffer_);
    }
    void *Allocate() throw()
    {
        buffer_ = ::operator new(sizeof(T), std::nothrow);
        return buffer_;
    }
    void Detach() throw() { buffer_ = nullptr; }
private:
    void *buffer_;
};

template <class I, class T> inline I *CastTo(T *p)
{
    if constexpr (IsSame<I, IUnknown>::value || IsSame<I, IInspectable>::value)
        return reinterpret_cast<I *>(p->CastToUnknown());
    else
        return static_cast<I *>(p);
}
} // namespace Details

/* a new T (count one) */
template <class T, class... Args> ComPtr<T> Make(Args &&...args) throw()
{
    ComPtr<T> o;
    Details::MakeAllocator<T> a;
    void *buf = a.Allocate();
    if (buf) {
        o.Attach(new (buf) T(Details::Forward<Args>(args)...));
        a.Detach();
    }
    return o;
}

namespace Details {
template <class T, class = void> struct HasInitialize { static const bool value = false; };
template <class T> struct HasInitialize<T, decltype((void)static_cast<T *>(nullptr)->RuntimeClassInitialize())>
{
    static const bool value = true;
};
template <class T, class... Args> inline HRESULT Initialize(T *p, Args &&...args) throw()
{
    return p->RuntimeClassInitialize(Forward<Args>(args)...);
}
/* a class without a RuntimeClassInitialize() needs none to be made */
template <class T> inline HRESULT Initialize(T *p) throw()
{
    if constexpr (HasInitialize<T>::value)
        return p->RuntimeClassInitialize();
    else
        return S_OK;
}
} // namespace Details

/* a new T, made ready by its RuntimeClassInitialize(args...) */
template <class T, class I, class... Args> HRESULT MakeAndInitialize(I **result, Args &&...args) throw()
{
    *result = nullptr;
    Details::MakeAllocator<T> a;
    void *buf = a.Allocate();
    if (!buf) return E_OUTOFMEMORY;
    T *p = new (buf) T();
    a.Detach();
    HRESULT hr = Details::Initialize(p, Details::Forward<Args>(args)...);
    if (FAILED(hr)) {
        p->Release();
        return hr;
    }
    *result = Details::CastTo<I>(p);
    return S_OK;
}
template <class T, class I, class... Args> HRESULT MakeAndInitialize(Details::ComPtrRef<ComPtr<I>> p, Args &&...args) throw()
{
    return MakeAndInitialize<T, I>(static_cast<I **>(p), Details::Forward<Args>(args)...);
}

}} // namespace Microsoft::WRL

/* the runtime class name and trust level a WRL class reports */
#define InspectableClass(runtimeClassName, trustLevel)                                       \
public:                                                                                      \
    static const wchar_t *STDMETHODCALLTYPE InternalGetRuntimeClassName() throw()            \
    {                                                                                        \
        return runtimeClassName;                                                             \
    }                                                                                        \
    static ::TrustLevel STDMETHODCALLTYPE InternalGetTrustLevel() throw() { return trustLevel; } \
    STDMETHOD(GetRuntimeClassName)(HSTRING * runtimeName) override                           \
    {                                                                                        \
        *runtimeName = nullptr;                                                              \
        const wchar_t *name = InternalGetRuntimeClassName();                                 \
        return name ? ::WindowsCreateString(name, static_cast<UINT32>(::wcslen(name)), runtimeName) : S_OK; \
    }                                                                                        \
    STDMETHOD(GetTrustLevel)(::TrustLevel * trustLvl) override                               \
    {                                                                                        \
        *trustLvl = trustLevel;                                                              \
        return S_OK;                                                                         \
    }                                                                                        \
                                                                                             \
private:

#define InspectableClassStatic(runtimeClassName, trustLevel)                                 \
public:                                                                                      \
    static const wchar_t *STDMETHODCALLTYPE InternalGetRuntimeClassNameStatic() throw()      \
    {                                                                                        \
        return runtimeClassName;                                                             \
    }                                                                                        \
    static ::TrustLevel STDMETHODCALLTYPE InternalGetTrustLevelStatic() throw() { return trustLevel; } \
    STDMETHOD(GetRuntimeClassName)(HSTRING *) override { return E_ILLEGAL_METHOD_CALL; }    \
    STDMETHOD(GetTrustLevel)(::TrustLevel * trustLvl) override                               \
    {                                                                                        \
        *trustLvl = trustLevel;                                                              \
        return S_OK;                                                                         \
    }                                                                                        \
                                                                                             \
private:
