/* winrt_abi.h — what the generated Windows Runtime ABI headers build on
 * (tools/winrt/abigen.py): IInspectable, IActivationFactory, the
 * Windows SDK's integer and boolean spellings, and the AggregateType /
 * GetAbiType / GetLogicalType templates generic instances use.  C++ only. */
#pragma once
#ifndef __cplusplus
#error The Windows Runtime ABI headers are C++
#endif
#include <windows.h>
#include <objbase.h>
#include <roapi.h>

typedef unsigned char boolean;
typedef double DOUBLE;
typedef float FLOAT;

/* __uuidof(IUnknown) for WRL-style code: give the declared interface its IID */
struct __declspec(uuid("00000000-0000-0000-C000-000000000046")) IUnknown;
struct __declspec(uuid("00000001-0000-0000-C000-000000000046")) IClassFactory;

#ifndef __IInspectable_INTERFACE_DEFINED__
#define __IInspectable_INTERFACE_DEFINED__
struct __declspec(uuid("AF86E2E0-B12D-4c6a-9C5A-D7AA65101E90")) __declspec(novtable) IInspectable : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG *iidCount, IID **iids) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *className) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *trustLevel) = 0;
};
#endif
#ifndef __IActivationFactory_INTERFACE_DEFINED__
#define __IActivationFactory_INTERFACE_DEFINED__
struct __declspec(uuid("00000035-0000-0000-C000-000000000046")) __declspec(novtable) IActivationFactory : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable **instance) = 0;
};
#endif

#ifndef DEFINE_ENUM_FLAG_OPERATORS
#define DEFINE_ENUM_FLAG_OPERATORS(E) \
    inline E operator|(E a, E b) { return E((unsigned int)a | (unsigned int)b); } \
    inline E operator&(E a, E b) { return E((unsigned int)a & (unsigned int)b); } \
    inline E operator^(E a, E b) { return E((unsigned int)a ^ (unsigned int)b); } \
    inline E operator~(E a) { return E(~(unsigned int)a); } \
    inline E &operator|=(E &a, E b) { return a = a | b; } \
    inline E &operator&=(E &a, E b) { return a = a & b; } \
    inline E &operator^=(E &a, E b) { return a = a ^ b; }
#endif

namespace ABI { namespace Windows { namespace Foundation { namespace Internal {
/* A generic argument whose logical type (a runtime class, bool) differs
 * from the type crossing the ABI (its default interface, boolean). */
template <class TLogical, class TAbi> struct AggregateType {};
template <class T> struct GetAbiType { typedef T type; };
template <class TLogical, class TAbi> struct GetAbiType<AggregateType<TLogical, TAbi>> { typedef TAbi type; };
template <class T> struct GetLogicalType { typedef T type; };
template <class TLogical, class TAbi> struct GetLogicalType<AggregateType<TLogical, TAbi>> { typedef TLogical type; };
}}}}
