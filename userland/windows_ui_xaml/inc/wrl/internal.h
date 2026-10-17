/* NovaOS's WRL (see wrl/client.h): its Details helpers */
#pragma once
#include <wrl/def.h>

namespace Microsoft { namespace WRL { namespace Details {

struct Nil {};
struct BoolStruct { int Member; };
typedef int BoolStruct::*BoolType;

template <bool b, typename T = void> struct EnableIf {};
template <typename T> struct EnableIf<true, T> { typedef T type; };

template <typename T1, typename T2> struct IsSame { static const bool value = false; };
template <typename T1> struct IsSame<T1, T1> { static const bool value = true; };

template <class T> struct RemoveReference { typedef T Type; };
template <class T> struct RemoveReference<T &> { typedef T Type; };
template <class T> struct RemoveReference<T &&> { typedef T Type; };

template <class T> inline T &&Forward(typename RemoveReference<T>::Type &arg) throw() { return static_cast<T &&>(arg); }
template <class T> inline typename RemoveReference<T>::Type &&Move(T &&arg) throw()
{
    return static_cast<typename RemoveReference<T>::Type &&>(arg);
}

template <class Base, class Derived> struct IsBaseOfStrict
{
    static const bool value = __is_base_of(Base, Derived) && !IsSame<Base, Derived>::value;
};

/* a plain read of *p that the compiler may not split or cache (the
 * Windows SDK's reads machine words with __iso_volatile_load*) */
template <class T> inline T ReadValueFromPointerNoFence(const volatile T *p)
{
    T v;
    __builtin_memcpy(&v, const_cast<const T *>(p), sizeof(T));
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    return v;
}

inline void RaiseException(HRESULT hr, DWORD flags = EXCEPTION_NONCONTINUABLE) throw()
{
    ::RaiseException(static_cast<DWORD>(hr), flags, 0, nullptr);
}

template <class T> inline void Swap(T &left, T &right) throw()
{
    T tmp = Move(left);
    left = Move(right);
    right = Move(tmp);
}

}}} // namespace Microsoft::WRL::Details
