/* NovaOS's WRL (see wrl/client.h): <wrl/event.h>.  Callback makes a
 * delegate object from a function (a lambda, or an object and its member
 * function); EventSource keeps an event's handlers and calls them. */
#pragma once
#include <eventtoken.h>
#include <wrl/implements.h>
#include <wrl/ftm.h>

namespace Microsoft { namespace WRL {

enum DelegateCheckMode { NoCheck = 1 };
enum InvokeMode { StopOnFirstError = 1, FireAll = 2 };
template <InvokeMode mode> struct InvokeModeOptions { static const InvokeMode invokeMode = mode; };
template <DelegateCheckMode mode = NoCheck> struct DelegateCheckModeOptions { static const DelegateCheckMode checkMode = mode; };
typedef InvokeModeOptions<StopOnFirstError> DefaultDelegateCheckMode;

namespace Details {

/* the argument list of a delegate interface's Invoke */
template <class... A> struct ArgList {};
template <class M> struct ArgTraits;
template <class C, class... A> struct ArgTraits<HRESULT (STDMETHODCALLTYPE C::*)(A...)>
{
    static const int args = sizeof...(A);
    typedef ArgList<A...> List;
    template <int n, class... B> struct At { typedef Nil type; };
    template <class B0, class... B> struct At<0, B0, B...> { typedef B0 type; };
    template <int n, class B0, class... B> struct At<n, B0, B...> { typedef typename At<n - 1, B...>::type type; };
    typedef typename At<0, A...>::type Arg1Type;
    typedef typename At<1, A...>::type Arg2Type;
    typedef typename At<2, A...>::type Arg3Type;
    typedef typename At<3, A...>::type Arg4Type;
    typedef typename At<4, A...>::type Arg5Type;
    typedef typename At<5, A...>::type Arg6Type;
    typedef typename At<6, A...>::type Arg7Type;
    typedef typename At<7, A...>::type Arg8Type;
    typedef typename At<8, A...>::type Arg9Type;
};

/* a Callback's template argument: the delegate interface, or an
 * Implements<flags, interface, ...> whose first interface is the delegate */
template <class T> struct DelegateOf { typedef T type; typedef T list; };
template <class F, class I, class... Rest> struct DelegateOf<Implements<F, I, Rest...>>
{
    typedef typename Conditional<IsFlags<F>::value, I, F>::type type;
    typedef Implements<F, I, Rest...> list;
};

template <class TDelegateInterface> struct ArgTraitsHelper
{
    typedef typename DelegateOf<TDelegateInterface>::type Interface;
    typedef decltype(&Interface::Invoke) methodType;
    typedef ArgTraits<methodType> Traits;
    static const int args = Traits::args;
};

/* the object Callback makes: TList (the delegate interface, or an
 * Implements) whose Invoke calls Fn */
template <class TList, class Fn, class Args> class DelegateImpl;
template <class TList, class Fn, class... A>
class DelegateImpl<TList, Fn, ArgList<A...>> : public RuntimeClass<RuntimeClassFlags<Delegate>, TList>
{
public:
    DelegateImpl(Fn &&fn) throw() : fn_(Move(fn)) {}
    DelegateImpl(const Fn &fn) throw() : fn_(fn) {}
    STDMETHOD(Invoke)(A... a) override { return fn_(a...); }
private:
    Fn fn_;
};

/* an object and one of its member functions, called as the delegate */
template <class TObject, class TMethod> struct BoundMethod
{
    TObject *object;
    TMethod method;
    template <class... A> HRESULT operator()(A... a) const { return (object->*method)(a...); }
};

typedef HRESULT(DelegateArgTraits_long_)(long);

} // namespace Details

template <class TDelegateInterface, class TCallback>
ComPtr<typename Details::DelegateOf<TDelegateInterface>::type> Callback(TCallback callback) throw()
{
    typedef Details::DelegateOf<TDelegateInterface> D;
    typedef typename Details::RemoveReference<TCallback>::Type Fn;
    typedef typename Details::ArgTraitsHelper<TDelegateInterface>::Traits::List Args;
    ComPtr<typename D::type> r;
    auto o = Make<Details::DelegateImpl<typename D::list, Fn, Args>>(Details::Move(callback));
    if (o) r = static_cast<typename D::type *>(o.Get());
    return r;
}

template <class TDelegateInterface, class TObject, class TMethod>
ComPtr<typename Details::DelegateOf<TDelegateInterface>::type> Callback(TObject *object, TMethod method) throw()
{
    return Callback<TDelegateInterface>(Details::BoundMethod<TObject, TMethod>{object, method});
}

/* an event's handlers: Add, Remove, InvokeAll */
template <class TDelegateInterface, class TEventSourceOptions = InvokeModeOptions<StopOnFirstError>>
class EventSource
{
public:
    EventSource() throw() : handlers_(nullptr), count_(0), capacity_(0), next_(1) { ::InitializeSRWLock(&lock_); }
    EventSource(const EventSource &) = delete;
    EventSource &operator=(const EventSource &) = delete;
    ~EventSource() throw() { Clear(); }

    HRESULT Add(TDelegateInterface *delegateInterface, EventRegistrationToken *token) throw()
    {
        if (!delegateInterface || !token) return E_INVALIDARG;
        ::AcquireSRWLockExclusive(&lock_);
        HRESULT hr = S_OK;
        if (count_ == capacity_) {
            unsigned int cap = capacity_ ? capacity_ * 2 : 4;
            Slot *n = static_cast<Slot *>(::CoTaskMemRealloc(handlers_, cap * sizeof(Slot)));
            if (!n) hr = E_OUTOFMEMORY;
            else {
                handlers_ = n;
                capacity_ = cap;
            }
        }
        if (SUCCEEDED(hr)) {
            delegateInterface->AddRef();
            handlers_[count_].handler = delegateInterface;
            handlers_[count_].token = next_++;
            token->value = handlers_[count_].token;
            count_++;
        }
        ::ReleaseSRWLockExclusive(&lock_);
        return hr;
    }
    HRESULT Remove(EventRegistrationToken token) throw()
    {
        TDelegateInterface *gone = nullptr;
        ::AcquireSRWLockExclusive(&lock_);
        for (unsigned int i = 0; i < count_; i++)
            if (handlers_[i].token == token.value) {
                gone = handlers_[i].handler;
                for (unsigned int j = i + 1; j < count_; j++) handlers_[j - 1] = handlers_[j];
                count_--;
                break;
            }
        ::ReleaseSRWLockExclusive(&lock_);
        if (gone) gone->Release();
        return S_OK;
    }
    unsigned int GetSize() const throw() { return count_; }

    template <class... A> HRESULT InvokeAll(A... a) throw()
    {
        /* a snapshot, so handlers may add and remove handlers */
        ::AcquireSRWLockShared(&lock_);
        unsigned int n = count_;
        TDelegateInterface **copy = n ? static_cast<TDelegateInterface **>(::CoTaskMemAlloc(n * sizeof(void *))) : nullptr;
        if (copy)
            for (unsigned int i = 0; i < n; i++) {
                copy[i] = handlers_[i].handler;
                copy[i]->AddRef();
            }
        ::ReleaseSRWLockShared(&lock_);
        if (n && !copy) return E_OUTOFMEMORY;
        HRESULT result = S_OK;
        for (unsigned int i = 0; i < n; i++) {
            HRESULT hr = (result == S_OK || TEventSourceOptions::invokeMode == FireAll) ? copy[i]->Invoke(a...) : S_OK;
            if (FAILED(hr) && SUCCEEDED(result) && TEventSourceOptions::invokeMode != FireAll) result = hr;
            copy[i]->Release();
        }
        ::CoTaskMemFree(copy);
        return result;
    }

protected:
    void Clear() throw()
    {
        for (unsigned int i = 0; i < count_; i++) handlers_[i].handler->Release();
        ::CoTaskMemFree(handlers_);
        handlers_ = nullptr;
        count_ = capacity_ = 0;
    }
    struct Slot
    {
        TDelegateInterface *handler;
        __int64 token;
    };
    SRWLOCK lock_;
    Slot *handlers_;
    unsigned int count_, capacity_;
    __int64 next_;
};

template <class TDelegateInterface, class TEventSourceOptions = InvokeModeOptions<StopOnFirstError>>
class AgileEventSource : public EventSource<TDelegateInterface, TEventSourceOptions> {};

}} // namespace Microsoft::WRL
