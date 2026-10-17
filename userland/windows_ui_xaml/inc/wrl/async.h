/* NovaOS's WRL (see wrl/client.h): <wrl/async.h>.  AsyncBase is the part
 * of an IAsyncAction or IAsyncOperation object that keeps its state
 * (created, started, completed, canceled, error, closed), answers
 * IAsyncInfo and calls its Completed and Progress handlers once each.
 * The class that derives from it supplies OnStart, OnCancel and OnClose and
 * its interface's put_Completed/get_Completed/GetResults through
 * PutOnComplete, GetOnComplete and CheckValidStateForResultsCall.  The
 * interface follows the Windows SDK's (its documentation); the inside is
 * NovaOS's own.  Causality tracing (AsyncCausalityOptions) is accepted and
 * compiled out: NovaOS has no causality tracer. */
#pragma once
#include <wrl/implements.h>
#include <windows.foundation.h>
#include <windows.foundation.diagnostics.h>

namespace Microsoft { namespace WRL {

enum AsyncResultType { SingleResult = 0x0001, MultipleResults = 0x0002 };
enum ErrorPropagationPolicy { PropagateDelegateError = 1, IgnoreDelegateError = 2 };

/* no causality tracing */
struct DisableCausality
{
    static const bool IsCausalityEnabled = false;
    static const ErrorPropagationPolicy PropagationPolicy = PropagateDelegateError;
};
/* causality tracing under the operation's name (compiled out) */
template <const wchar_t *OpName, const GUID &PlatformId = GUID_NULL,
          ABI::Windows::Foundation::Diagnostics::CausalitySource Source =
              ABI::Windows::Foundation::Diagnostics::CausalitySource_System>
struct AsyncCausalityOptions
{
    static const bool IsCausalityEnabled = true;
    static const ErrorPropagationPolicy PropagationPolicy = PropagateDelegateError;
    static const wchar_t *GetAsyncOperationName() { return OpName; }
};
template <ErrorPropagationPolicy policy = PropagateDelegateError, class CausalityOptions = DisableCausality>
struct AsyncOptions : CausalityOptions
{
    static const ErrorPropagationPolicy PropagationPolicy = policy;
};

namespace Details {
enum AsyncStatusInternal
{
    _Created = -1,
    _Started = static_cast<int>(ABI::Windows::Foundation::AsyncStatus::Started),
    _Completed = static_cast<int>(ABI::Windows::Foundation::AsyncStatus::Completed),
    _Canceled = static_cast<int>(ABI::Windows::Foundation::AsyncStatus::Canceled),
    _Error = static_cast<int>(ABI::Windows::Foundation::AsyncStatus::Error),
    _Closed = 4,
};
/* a handler's Invoke(IAsyncX *asyncInfo, Arg2 arg): its argument types */
template <class M> struct InvokeTraits;
template <class C, class A1, class A2> struct InvokeTraits<HRESULT (STDMETHODCALLTYPE C::*)(A1, A2)>
{
    typedef A1 Arg1Type;
    typedef A2 Arg2Type;
};
template <class T> struct PointeeOf;
template <class T> struct PointeeOf<T *> { typedef T type; };
template <class D> struct DelegateTraits
{
    typedef InvokeTraits<decltype(&D::Invoke)> Traits;
    typedef typename Traits::Arg1Type Arg1Type;           // the async interface
    typedef typename Traits::Arg2Type Arg2Type;
    typedef typename PointeeOf<Arg1Type>::type Interface;
};
} // namespace Details

template <class TComplete, class TProgress = Details::Nil, AsyncResultType resultType = SingleResult,
          class TAsyncBaseOptions = AsyncOptions<>>
class AsyncBase;

template <class TComplete, AsyncResultType resultType, class TAsyncBaseOptions>
class AsyncBase<TComplete, Details::Nil, resultType, TAsyncBaseOptions>
    : public Implements<ABI::Windows::Foundation::IAsyncInfo>
{
public:
    AsyncBase() throw() : status_(Details::_Created), error_(S_OK), id_(1), fired_(0) {}

    /* IAsyncInfo */
    STDMETHOD(get_Id)(unsigned int *id) override
    {
        if (!id) return E_POINTER;
        if (Status() == Details::_Closed) return E_ILLEGAL_METHOD_CALL;
        *id = id_;
        return S_OK;
    }
    STDMETHOD(put_Id)(unsigned int id)
    {
        if (id == 0) return E_INVALIDARG;
        if (Status() != Details::_Created) return E_ILLEGAL_METHOD_CALL;
        id_ = id;
        return S_OK;
    }
    STDMETHOD(get_Status)(ABI::Windows::Foundation::AsyncStatus *status) override
    {
        if (!status) return E_POINTER;
        Details::AsyncStatusInternal s = Status();
        if (s == Details::_Closed) return E_ILLEGAL_METHOD_CALL;
        *status = s == Details::_Created ? ABI::Windows::Foundation::AsyncStatus::Started
                                         : static_cast<ABI::Windows::Foundation::AsyncStatus>(s);
        return S_OK;
    }
    STDMETHOD(get_ErrorCode)(HRESULT *errorCode) override
    {
        if (!errorCode) return E_POINTER;
        if (Status() == Details::_Closed) return E_ILLEGAL_METHOD_CALL;
        *errorCode = Status() == Details::_Canceled ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : error_;
        return S_OK;
    }
    STDMETHOD(Cancel)() override
    {
        if (Transition(Details::_Started, Details::_Canceled) || Transition(Details::_Created, Details::_Canceled))
            OnCancel();
        else if (Status() == Details::_Closed)
            return E_ILLEGAL_METHOD_CALL;
        return S_OK;
    }
    STDMETHOD(Close)() override
    {
        for (;;)
        {
            Details::AsyncStatusInternal s = Status();
            if (s == Details::_Closed) return S_OK;
            if (s == Details::_Started || s == Details::_Created) return E_ILLEGAL_STATE_CHANGE;
            if (Transition(s, Details::_Closed))
            {
                OnClose();
                return S_OK;
            }
        }
    }

protected:
    virtual HRESULT OnStart() = 0;
    virtual void OnClose() = 0;
    virtual void OnCancel() = 0;

    /* created -> started, then OnStart (a failure from it is the error) */
    HRESULT Start()
    {
        if (!Transition(Details::_Created, Details::_Started))
            return Status() == Details::_Closed ? E_ILLEGAL_METHOD_CALL : E_ILLEGAL_STATE_CHANGE;
        HRESULT hr = OnStart();
        if (FAILED(hr))
        {
            TryTransitionToError(hr);
            FireCompletion();
        }
        return hr;
    }
    bool ContinueAsyncOperation() { return Status() == Details::_Started; }
    bool TryTransitionToCompleted() { return Transition(Details::_Started, Details::_Completed) || Status() == Details::_Completed; }
    bool TryTransitionToError(HRESULT error)
    {
        if (!Transition(Details::_Started, Details::_Error)) return false;
        error_ = error;
        return true;
    }
    void CurrentStatus(Details::AsyncStatusInternal *status) { *status = Status(); }
    void ErrorCode(HRESULT *error) { *error = error_; }

    HRESULT CheckValidStateForDelegateCall()
    {
        return Status() == Details::_Closed ? E_ILLEGAL_METHOD_CALL : S_OK;
    }
    /* GetResults: valid once completed (and while started, for an
       operation with several results); an error state gives its error */
    HRESULT CheckValidStateForResultsCall()
    {
        Details::AsyncStatusInternal s = Status();
        if (s == Details::_Error) return error_;
        if (s == Details::_Completed) return S_OK;
        if (resultType == MultipleResults && s == Details::_Started) return S_OK;
        return E_ILLEGAL_METHOD_CALL;
    }

    /* the Completed handler: set once; one set after completion is called at once */
    HRESULT PutOnComplete(TComplete *handler)
    {
        HRESULT hr = CheckValidStateForDelegateCall();
        if (FAILED(hr)) return hr;
        if (::InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(&complete_), handler, nullptr) != nullptr)
            return E_ILLEGAL_DELEGATE_ASSIGNMENT;
        if (handler) handler->AddRef();
        Details::AsyncStatusInternal s = Status();
        if (s == Details::_Completed || s == Details::_Canceled || s == Details::_Error)
            FireCompletion();
        return S_OK;
    }
    HRESULT GetOnComplete(TComplete **handler)
    {
        if (!handler) return E_POINTER;
        HRESULT hr = CheckValidStateForDelegateCall();
        if (FAILED(hr)) return hr;
        *handler = complete_;
        if (*handler) (*handler)->AddRef();
        return S_OK;
    }
    /* completes the operation if it is still running, then calls the
       Completed handler (once) with the state it ended in */
    void FireCompletion()
    {
        TryTransitionToCompleted();
        TComplete *handler = complete_;
        if (!handler || ::InterlockedCompareExchange(&fired_, 1, 0) != 0)
            return;
        ComPtr<typename Details::DelegateTraits<TComplete>::Interface> self;
        if (SUCCEEDED(static_cast<ABI::Windows::Foundation::IAsyncInfo *>(this)->QueryInterface(
                __uuidof(typename Details::DelegateTraits<TComplete>::Interface),
                reinterpret_cast<void **>(self.GetAddressOf()))))
        {
            ABI::Windows::Foundation::AsyncStatus status;
            Details::AsyncStatusInternal s = Status();
            status = s == Details::_Closed ? ABI::Windows::Foundation::AsyncStatus::Completed
                                           : static_cast<ABI::Windows::Foundation::AsyncStatus>(s);
            handler->Invoke(self.Get(), status);
        }
    }

    ~AsyncBase()
    {
        if (complete_) complete_->Release();
    }

    Details::AsyncStatusInternal Status() const { return static_cast<Details::AsyncStatusInternal>(status_); }
    bool Transition(Details::AsyncStatusInternal from, Details::AsyncStatusInternal to)
    {
        return ::InterlockedCompareExchange(&status_, to, from) == from;
    }

private:
    volatile long status_;
    HRESULT error_;
    unsigned int id_;
    volatile long fired_;
    TComplete *volatile complete_ = nullptr;
};

/* with a Progress handler too */
template <class TComplete, class TProgress, AsyncResultType resultType, class TAsyncBaseOptions>
class AsyncBase : public AsyncBase<TComplete, Details::Nil, resultType, TAsyncBaseOptions>
{
    typedef AsyncBase<TComplete, Details::Nil, resultType, TAsyncBaseOptions> Base;

protected:
    HRESULT PutOnProgress(TProgress *handler)
    {
        HRESULT hr = this->CheckValidStateForDelegateCall();
        if (FAILED(hr)) return hr;
        ComPtr<TProgress> old = progress_;
        progress_ = handler;
        return S_OK;
    }
    HRESULT GetOnProgress(TProgress **handler)
    {
        if (!handler) return E_POINTER;
        HRESULT hr = this->CheckValidStateForDelegateCall();
        if (FAILED(hr)) return hr;
        return progress_.CopyTo(handler);
    }
    /* the Progress handler, while the operation runs */
    void FireProgress(typename Details::DelegateTraits<TProgress>::Arg2Type arg)
    {
        if (!progress_ || this->Status() != Details::_Started)
            return;
        ComPtr<typename Details::DelegateTraits<TProgress>::Interface> self;
        if (SUCCEEDED(static_cast<ABI::Windows::Foundation::IAsyncInfo *>(this)->QueryInterface(
                __uuidof(typename Details::DelegateTraits<TProgress>::Interface),
                reinterpret_cast<void **>(self.GetAddressOf()))))
            progress_->Invoke(self.Get(), arg);
    }

private:
    ComPtr<TProgress> progress_;
};

}} // namespace Microsoft::WRL
