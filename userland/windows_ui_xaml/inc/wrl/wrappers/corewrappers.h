/* NovaOS's WRL (see wrl/client.h): <wrl/wrappers/corewrappers.h>, owners of
 * HSTRINGs, handles and locks */
#pragma once
#include <windows.h>
#include <winstring.h>
#include <roapi.h>
#include <wrl/def.h>
#include <wrl/internal.h>

namespace Microsoft { namespace WRL { namespace Wrappers {

class HStringReference;

/* an HSTRING this object owns */
class HString
{
public:
    HString() throw() : hstr_(nullptr) {}
    HString(HString &&other) throw() : hstr_(other.hstr_) { other.hstr_ = nullptr; }
    HString(const HString &) = delete;
    HString &operator=(const HString &) = delete;
    HString &operator=(HString &&other) throw()
    {
        Release();
        hstr_ = other.hstr_;
        other.hstr_ = nullptr;
        return *this;
    }
    ~HString() throw() { Release(); }

    HRESULT Set(const wchar_t *str) throw() { return Set(str, str ? static_cast<unsigned int>(::wcslen(str)) : 0); }
    HRESULT Set(const wchar_t *str, unsigned int len) throw()
    {
        Release();
        return ::WindowsCreateString(str, len, &hstr_);
    }
    template <size_t s> HRESULT Set(const wchar_t (&str)[s]) throw() { return Set(str, s - 1); }
    HRESULT Set(const HSTRING &hstr) throw()
    {
        if (hstr == hstr_) return S_OK;
        Release();
        return ::WindowsDuplicateString(hstr, &hstr_);
    }
    void Attach(HSTRING hstr) throw()
    {
        if (hstr == hstr_) return;
        Release();
        hstr_ = hstr;
    }
    HSTRING Detach() throw()
    {
        HSTRING h = hstr_;
        hstr_ = nullptr;
        return h;
    }
    HSTRING *GetAddressOf() throw()
    {
        Release();
        return &hstr_;
    }
    HSTRING *ReleaseAndGetAddressOf() throw() { return GetAddressOf(); }
    HSTRING Get() const throw() { return hstr_; }
    /* (as MSVC's WRL is used: an HString passed where an HSTRING is taken,
     * Application's launch arguments, dxaml/lib FrameworkApplication_Partial.cpp) */
    operator HSTRING() const throw() { return hstr_; }
    void Release() throw()
    {
        if (hstr_) {
            ::WindowsDeleteString(hstr_);
            hstr_ = nullptr;
        }
    }
    bool IsValid() const throw() { return hstr_ != nullptr; }
    UINT32 Length() const throw() { return ::WindowsGetStringLen(hstr_); }
    BOOL IsEmpty() const throw() { return ::WindowsIsStringEmpty(hstr_); }
    const wchar_t *GetRawBuffer(unsigned int *length) const throw() { return ::WindowsGetStringRawBuffer(hstr_, length); }
    HRESULT CopyTo(HSTRING *str) const throw() { return ::WindowsDuplicateString(hstr_, str); }
    HRESULT Duplicate(const HString &other) throw() { return Set(other.hstr_); }
    /* (each result is made before newString lets go of its own, so newString
     * may be this string: Popup_Partial.cpp's truncation does so) */
    HRESULT Concat(const HString &str, HString &newString) const throw()
    {
        HSTRING h = nullptr;
        HRESULT hr = ::WindowsConcatString(hstr_, str.hstr_, &h);
        if (SUCCEEDED(hr)) newString.Take(h);
        return hr;
    }
    HRESULT TrimStart(const HString &trim, HString &newString) const throw()
    {
        HSTRING h = nullptr;
        HRESULT hr = ::WindowsTrimStringStart(hstr_, trim.hstr_, &h);
        if (SUCCEEDED(hr)) newString.Take(h);
        return hr;
    }
    HRESULT TrimEnd(const HString &trim, HString &newString) const throw()
    {
        HSTRING h = nullptr;
        HRESULT hr = ::WindowsTrimStringEnd(hstr_, trim.hstr_, &h);
        if (SUCCEEDED(hr)) newString.Take(h);
        return hr;
    }
    HRESULT Substring(UINT32 startIndex, HString &newString) const throw()
    {
        HSTRING h = nullptr;
        HRESULT hr = ::WindowsSubstring(hstr_, startIndex, &h);
        if (SUCCEEDED(hr)) newString.Take(h);
        return hr;
    }
    HRESULT Substring(UINT32 startIndex, UINT32 length, HString &newString) const throw()
    {
        HSTRING h = nullptr;
        HRESULT hr = ::WindowsSubstringWithSpecifiedLength(hstr_, startIndex, length, &h);
        if (SUCCEEDED(hr)) newString.Take(h);
        return hr;
    }
    void Swap(HString &other) throw()
    {
        HSTRING h = hstr_;
        hstr_ = other.hstr_;
        other.hstr_ = h;
    }

    static HStringReference MakeReference(const wchar_t *str) throw();
    static HStringReference MakeReference(const wchar_t *str, unsigned int len) throw();
    template <unsigned int s> static HStringReference MakeReference(const wchar_t (&str)[s]) throw();

protected:
    /* owns h, a new reference (a duplicate of hstr_ itself drops its extra one) */
    void Take(HSTRING h) throw()
    {
        if (h == hstr_) {
            if (h) ::WindowsDeleteString(h);
            return;
        }
        Release();
        hstr_ = h;
    }

    HSTRING hstr_;
};

/* an HSTRING over a buffer the caller keeps alive (WindowsCreateStringReference) */
class HStringReference
{
public:
    HStringReference(const wchar_t *str, unsigned int len) throw() : hstr_(nullptr) { CreateReference(str, len, len); }
    template <unsigned int s> HStringReference(const wchar_t (&str)[s]) throw() : hstr_(nullptr) { CreateReference(str, s - 1, s - 1); }
    template <unsigned int s> HStringReference(wchar_t (&str)[s]) throw() : hstr_(nullptr)
    {
        CreateReference(str, static_cast<unsigned int>(::wcslen(str)), s - 1);
    }
    explicit HStringReference(const wchar_t *str) throw() : hstr_(nullptr)
    {
        unsigned int len = str ? static_cast<unsigned int>(::wcslen(str)) : 0;
        CreateReference(str, len, len);
    }
    HStringReference(const HStringReference &other) throw() : hstr_(nullptr)
    {
        unsigned int len = 0;
        const wchar_t *v = other.GetRawBuffer(&len);
        CreateReference(v, len, len);
    }
    HStringReference &operator=(const HStringReference &other) throw()
    {
        unsigned int len = 0;
        const wchar_t *v = other.GetRawBuffer(&len);
        CreateReference(v, len, len);
        return *this;
    }
    HStringReference(HStringReference &&other) throw() : header_(other.header_), hstr_(nullptr)
    {
        if (other.hstr_) hstr_ = reinterpret_cast<HSTRING>(&header_);
        other.hstr_ = nullptr;
    }
    HStringReference &operator=(HStringReference &&other) throw()
    {
        header_ = other.header_;
        hstr_ = other.hstr_ ? reinterpret_cast<HSTRING>(&header_) : nullptr;
        other.hstr_ = nullptr;
        return *this;
    }

    HSTRING Get() const throw() { return hstr_; }
    const wchar_t *GetRawBuffer(unsigned int *length) const throw() { return ::WindowsGetStringRawBuffer(hstr_, length); }
    HRESULT CopyTo(HSTRING *str) const throw() { return ::WindowsDuplicateString(hstr_, str); }
    UINT32 Length() const throw() { return ::WindowsGetStringLen(hstr_); }
    BOOL IsEmpty() const throw() { return ::WindowsIsStringEmpty(hstr_); }

protected:
    void CreateReference(const wchar_t *str, unsigned int bufferLen, unsigned int len)
    {
        (void)bufferLen;
        hstr_ = nullptr;
        if (len == 0) return;       /* the empty string is the null HSTRING */
        HRESULT hr = ::WindowsCreateStringReference(str, len, &header_, &hstr_);
        if (FAILED(hr)) ::Microsoft::WRL::Details::RaiseException(hr);
    }

    HSTRING_HEADER header_;
    HSTRING hstr_;
};

inline HStringReference HString::MakeReference(const wchar_t *str) throw() { return HStringReference(str); }
inline HStringReference HString::MakeReference(const wchar_t *str, unsigned int len) throw() { return HStringReference(str, len); }
template <unsigned int s> inline HStringReference HString::MakeReference(const wchar_t (&str)[s]) throw() { return HStringReference(str); }

inline bool operator==(const HString &l, const HString &r) throw()
{
    INT32 res = 0;
    return SUCCEEDED(::WindowsCompareStringOrdinal(l.Get(), r.Get(), &res)) && res == 0;
}
inline bool operator==(const HStringReference &l, const HString &r) throw()
{
    INT32 res = 0;
    return SUCCEEDED(::WindowsCompareStringOrdinal(l.Get(), r.Get(), &res)) && res == 0;
}
inline bool operator==(const HString &l, const HStringReference &r) throw() { return r == l; }
inline bool operator==(const HSTRING &l, const HString &r) throw()
{
    INT32 res = 0;
    return SUCCEEDED(::WindowsCompareStringOrdinal(l, r.Get(), &res)) && res == 0;
}
inline bool operator==(const HString &l, const HSTRING &r) throw() { return r == l; }
inline bool operator==(const HStringReference &l, const HStringReference &r) throw()
{
    INT32 res = 0;
    return SUCCEEDED(::WindowsCompareStringOrdinal(l.Get(), r.Get(), &res)) && res == 0;
}
inline bool operator==(const HSTRING &l, const HStringReference &r) throw()
{
    INT32 res = 0;
    return SUCCEEDED(::WindowsCompareStringOrdinal(l, r.Get(), &res)) && res == 0;
}
inline bool operator==(const HStringReference &l, const HSTRING &r) throw() { return r == l; }
inline bool operator!=(const HStringReference &l, const HStringReference &r) throw() { return !(l == r); }
inline bool operator!=(const HSTRING &l, const HStringReference &r) throw() { return !(l == r); }
inline bool operator!=(const HStringReference &l, const HSTRING &r) throw() { return !(l == r); }
inline bool operator!=(const HString &l, const HString &r) throw() { return !(l == r); }
inline bool operator!=(const HStringReference &l, const HString &r) throw() { return !(l == r); }
inline bool operator!=(const HString &l, const HStringReference &r) throw() { return !(l == r); }
inline bool operator!=(const HSTRING &l, const HString &r) throw() { return !(l == r); }
inline bool operator!=(const HString &l, const HSTRING &r) throw() { return !(l == r); }
inline bool operator<(const HString &l, const HString &r) throw()
{
    INT32 res = 0;
    return SUCCEEDED(::WindowsCompareStringOrdinal(l.Get(), r.Get(), &res)) && res < 0;
}

namespace HandleTraits {
struct HANDLENullTraits
{
    typedef HANDLE Type;
    static bool Close(Type h) throw() { return ::CloseHandle(h) != FALSE; }
    static Type GetInvalidValue() throw() { return nullptr; }
};
struct HANDLETraits
{
    typedef HANDLE Type;
    static bool Close(Type h) throw() { return ::CloseHandle(h) != FALSE; }
    static Type GetInvalidValue() throw() { return INVALID_HANDLE_VALUE; }
};
struct EventTraits : HANDLENullTraits {};
struct MutexTraits : HANDLENullTraits {};
struct SemaphoreTraits : HANDLENullTraits {};
struct FileHandleTraits : HANDLETraits {};
} // namespace HandleTraits

template <class Traits>
class HandleT
{
public:
    explicit HandleT(typename Traits::Type h = Traits::GetInvalidValue()) throw() : handle_(h) {}
    HandleT(HandleT &&h) throw() : handle_(h.handle_) { h.handle_ = Traits::GetInvalidValue(); }
    HandleT(const HandleT &) = delete;
    HandleT &operator=(const HandleT &) = delete;
    HandleT &operator=(HandleT &&h) throw()
    {
        Close();
        handle_ = h.handle_;
        h.handle_ = Traits::GetInvalidValue();
        return *this;
    }
    ~HandleT() throw() { Close(); }
    void Close() throw()
    {
        if (handle_ != Traits::GetInvalidValue()) {
            Traits::Close(handle_);
            handle_ = Traits::GetInvalidValue();
        }
    }
    bool IsValid() const throw() { return handle_ != Traits::GetInvalidValue(); }
    typename Traits::Type Get() const throw() { return handle_; }
    typename Traits::Type *GetAddressOf() throw() { return &handle_; }
    typename Traits::Type *ReleaseAndGetAddressOf() throw() { Close(); return &handle_; }
    void Attach(typename Traits::Type h) throw()
    {
        if (h != handle_) {
            Close();
            handle_ = h;
        }
    }
    typename Traits::Type Detach() throw()
    {
        typename Traits::Type h = handle_;
        handle_ = Traits::GetInvalidValue();
        return h;
    }
protected:
    typename Traits::Type handle_;
};

typedef HandleT<HandleTraits::HANDLETraits> FileHandle;
typedef HandleT<HandleTraits::EventTraits> Event;
typedef HandleT<HandleTraits::MutexTraits> Mutex;
typedef HandleT<HandleTraits::SemaphoreTraits> Semaphore;

namespace Details {
template <class SyncTraits>
class SyncLockT
{
public:
    SyncLockT(SyncLockT &&other) throw() : sync_(other.sync_) { other.sync_ = SyncTraits::GetInvalidValue(); }
    SyncLockT(const SyncLockT &) = delete;
    SyncLockT &operator=(const SyncLockT &) = delete;
    ~SyncLockT() throw() { InternalUnlock(); }
    void Unlock() throw() { InternalUnlock(); }
    bool IsLocked() const throw() { return sync_ != SyncTraits::GetInvalidValue(); }
protected:
    explicit SyncLockT(typename SyncTraits::Type sync = SyncTraits::GetInvalidValue()) throw() : sync_(sync) {}
    void InternalUnlock() throw()
    {
        if (IsLocked()) {
            SyncTraits::Unlock(sync_);
            sync_ = SyncTraits::GetInvalidValue();
        }
    }
    typename SyncTraits::Type sync_;
};
} // namespace Details

namespace SyncTraits {
struct CriticalSectionTraits
{
    typedef CRITICAL_SECTION *Type;
    static Type GetInvalidValue() throw() { return nullptr; }
    static void Unlock(Type cs) throw() { ::LeaveCriticalSection(cs); }
};
struct SRWLockExclusiveTraits
{
    typedef SRWLOCK *Type;
    static Type GetInvalidValue() throw() { return nullptr; }
    static void Unlock(Type l) throw() { ::ReleaseSRWLockExclusive(l); }
};
struct SRWLockSharedTraits
{
    typedef SRWLOCK *Type;
    static Type GetInvalidValue() throw() { return nullptr; }
    static void Unlock(Type l) throw() { ::ReleaseSRWLockShared(l); }
};
} // namespace SyncTraits

class CriticalSection
{
public:
    class SyncLock : public Details::SyncLockT<SyncTraits::CriticalSectionTraits>
    {
    public:
        explicit SyncLock(CRITICAL_SECTION *cs = nullptr) throw() : SyncLockT(cs) {}
    };
    explicit CriticalSection(ULONG spincount = 0) throw() { ::InitializeCriticalSectionAndSpinCount(&cs_, spincount); }
    CriticalSection(const CriticalSection &) = delete;
    CriticalSection &operator=(const CriticalSection &) = delete;
    ~CriticalSection() throw() { ::DeleteCriticalSection(&cs_); }
    SyncLock Lock() throw() { return Lock(&cs_); }
    static SyncLock Lock(CRITICAL_SECTION *cs) throw()
    {
        ::EnterCriticalSection(cs);
        return SyncLock(cs);
    }
    SyncLock TryLock() throw() { return TryLock(&cs_); }
    static SyncLock TryLock(CRITICAL_SECTION *cs) throw()
    {
        return SyncLock(::TryEnterCriticalSection(cs) ? cs : nullptr);
    }
    bool IsValid() const throw() { return true; }
protected:
    CRITICAL_SECTION cs_;
};

class SRWLock
{
public:
    class SyncLockExclusive : public Details::SyncLockT<SyncTraits::SRWLockExclusiveTraits>
    {
    public:
        explicit SyncLockExclusive(SRWLOCK *l = nullptr) throw() : SyncLockT(l) {}
    };
    class SyncLockShared : public Details::SyncLockT<SyncTraits::SRWLockSharedTraits>
    {
    public:
        explicit SyncLockShared(SRWLOCK *l = nullptr) throw() : SyncLockT(l) {}
    };
    SRWLock() throw() { ::InitializeSRWLock(&lock_); }
    SRWLock(const SRWLock &) = delete;
    SRWLock &operator=(const SRWLock &) = delete;
    SyncLockExclusive LockExclusive() throw() { return LockExclusive(&lock_); }
    static SyncLockExclusive LockExclusive(SRWLOCK *l) throw()
    {
        ::AcquireSRWLockExclusive(l);
        return SyncLockExclusive(l);
    }
    SyncLockShared LockShared() throw() { return LockShared(&lock_); }
    static SyncLockShared LockShared(SRWLOCK *l) throw()
    {
        ::AcquireSRWLockShared(l);
        return SyncLockShared(l);
    }
protected:
    SRWLOCK lock_;
};

class RoInitializeWrapper
{
public:
    RoInitializeWrapper(RO_INIT_TYPE flags) throw() { hr_ = ::RoInitialize(flags); }
    ~RoInitializeWrapper() throw()
    {
        if (SUCCEEDED(hr_)) ::RoUninitialize();
    }
    operator HRESULT() const throw() { return hr_; }
private:
    HRESULT hr_;
};

}}} // namespace Microsoft::WRL::Wrappers
