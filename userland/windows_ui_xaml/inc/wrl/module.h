/* NovaOS's WRL (see wrl/client.h): <wrl/module.h>.  The module object
 * counts the objects a DLL keeps alive (for DllCanUnloadNow); NovaOS's
 * XAML registers its activation factories itself, so the class-factory
 * registry Windows' WRL keeps here is not needed. */
#pragma once
#include <wrl/implements.h>

namespace Microsoft { namespace WRL {

enum ModuleType { InProc = 0x01, OutOfProc = 0x02, DisableCaching = 0x04, InProcDisableCaching = InProc | DisableCaching,
                  OutOfProcDisableCaching = OutOfProc | DisableCaching };

namespace Details {
class ModuleBase
{
public:
    ModuleBase() throw() : objectCount_(0) {}
    virtual ~ModuleBase() throw() {}
    virtual unsigned long IncrementObjectCount() throw() { return ::InterlockedIncrement(&objectCount_); }
    virtual unsigned long DecrementObjectCount() throw() { return ::InterlockedDecrement(&objectCount_); }
    virtual unsigned long GetObjectCount() const throw() { return objectCount_; }
protected:
    volatile long objectCount_;
};
} // namespace Details

template <int moduleType = InProc, class ModuleT = void>
class Module : public Details::ModuleBase
{
public:
    static Module &GetModule() throw()
    {
        static Module m;
        return m;
    }
    static Module &Create() throw() { return GetModule(); }
    bool Terminate(const wchar_t * = nullptr, bool = false) throw() { return GetObjectCount() == 0; }
    HRESULT GetActivationFactory(HSTRING, IActivationFactory **factory, const wchar_t * = nullptr) throw()
    {
        *factory = nullptr;
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    HRESULT GetClassObject(REFCLSID, REFIID, void **ppv, const wchar_t * = nullptr) throw()
    {
        *ppv = nullptr;
        return CLASS_E_CLASSNOTAVAILABLE;
    }
};

inline Details::ModuleBase *GetModuleBase() throw() { return &Module<InProc>::GetModule(); }

}} // namespace Microsoft::WRL

/* a class's entry in the module's registry of activatable (and
 * CoCreateInstance-able) classes, which NovaOS's module does not keep
 * (above): nothing */
#ifndef ActivatableClass
#define ActivatableClass(className)
#define ActivatableClassWithFactory(className, factory)
#define ActivatableStaticOnlyFactory(factory)
#endif
#ifndef CoCreatableClass
#define CoCreatableClass(className)
#endif
