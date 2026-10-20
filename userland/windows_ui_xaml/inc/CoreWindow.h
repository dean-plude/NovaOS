/* NovaOS: the Windows SDK's corewindow.h, which MinGW-w64 lacks: a
 * CoreWindow's classic COM side, ICoreWindowInterop, which gives its HWND
 * (NovaOS's twinapi.appcore implements it; XAML's DXamlCore reads the
 * window it is hosted in through it). */
#pragma once
#include <windows.h>
#include <unknwn.h>

#ifndef __ICoreWindowInterop_INTERFACE_DEFINED__
#define __ICoreWindowInterop_INTERFACE_DEFINED__
struct __declspec(uuid("45D64A29-A63E-4CB6-B498-5781D298CB4F")) __declspec(novtable) ICoreWindowInterop : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE get_WindowHandle(HWND *hwnd) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MessageHandled(unsigned char value) = 0;
};
#endif
