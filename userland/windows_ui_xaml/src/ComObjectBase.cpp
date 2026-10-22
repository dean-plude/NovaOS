// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.
//
// NovaOS: XAML's ComObjectBase.cpp (components/com), the framework objects'
// QueryInterface, with the Windows.UI.Xaml faces: an interface ID the
// object has no answer for goes to nova_xaml_face_query (a Windows.UI.Xaml
// interface over the framework's, src/windows_faces.cpp), and a framework
// overridable interface asked of a composed object itself, which its outer
// passed on, to nova_xaml_override_face (the outer's Windows overrides: a
// WinUI 2 control's MeasureOverride); and one neither the object nor its
// faces answer, to nova_xaml_outer_face (the outer's Windows interface: a
// WinUI 2 app's IXamlMetadataProvider).  A program without the faces (the
// component tests) links the answers below, "no interface".
#include "precomp.h"
#include "ComObjectBase.h"

extern "C" HRESULT nova_xaml_face_query(REFIID iid, void **out, HRESULT (*impl)(void *, REFIID, void **), void *ctx);
extern "C" HRESULT nova_xaml_override_face(REFIID iid, IInspectable *outer, void **out);
extern "C" HRESULT nova_xaml_outer_face(REFIID iid, IInspectable *outer, void **out);
extern "C" HRESULT nova_xaml_no_face(REFIID, void **, HRESULT (*)(void *, REFIID, void **), void *)
{
    return E_NOINTERFACE;
}
extern "C" HRESULT nova_xaml_no_override_face(REFIID, IInspectable *, void **)
{
    return E_NOINTERFACE;
}
#pragma comment(linker, "/alternatename:nova_xaml_face_query=nova_xaml_no_face")
#pragma comment(linker, "/alternatename:nova_xaml_override_face=nova_xaml_no_override_face")
#pragma comment(linker, "/alternatename:nova_xaml_outer_face=nova_xaml_no_override_face")

static HRESULT own_query(void *self, REFIID iid, void **out)
{
    return static_cast<ctl::ComObjectBase *>(self)->QueryInterfaceImplBase(iid, out);
}

IFACEMETHODIMP ctl::ComObjectBase::NonDelegatingQueryInterfaceBase(REFIID iid, void **ppValue)
{
    IUnknown* pInterface = nullptr;

    if (InlineIsEqualGUID(iid, IID_IUnknown))
    {
        pInterface = reinterpret_cast<IUnknown *>(static_cast<INonDelegatingUnknown *>(this));
    }
    else if (InlineIsEqualGUID(iid, IID_IInspectable))
    {
        pInterface = reinterpret_cast<IInspectable *>(static_cast<INonDelegatingInspectable *>(this));
    }
    else
    {
        if (m_pControllingUnknown && SUCCEEDED(nova_xaml_override_face(iid, m_pControllingUnknown, ppValue)))
        {
            return S_OK;
        }
        HRESULT hr = QueryInterfaceImplBase(iid, ppValue);
        if (hr == E_NOINTERFACE)
        {
            hr = nova_xaml_face_query(iid, ppValue, own_query, this);
        }
        if (hr == E_NOINTERFACE && m_pControllingUnknown)
        {
            hr = nova_xaml_outer_face(iid, m_pControllingUnknown, ppValue);
        }
        return hr;
    }

    *ppValue = pInterface;
    pInterface->AddRef();
    return S_OK;
}

IFACEMETHODIMP ctl::ComObjectBase::QueryInterfaceBase(REFIID iid, void **ppValue)
{
    if (m_pControllingUnknown)
    {
        return m_pControllingUnknown->QueryInterface(iid, ppValue);
    }
    else
    {
        HRESULT hr = QueryInterfaceImplBase(iid, ppValue);
        if (hr == E_NOINTERFACE)
        {
            hr = nova_xaml_face_query(iid, ppValue, own_query, this);
        }
        return hr;
    }
}


HRESULT ctl::ComObjectBase::CreateInstanceBase(_In_ ComBase* pNewInstance, bool fNoInit)
{
    if (!fNoInit)
    {
        IFC_RETURN(pNewInstance->Initialize());
    }

    return S_OK;
}
