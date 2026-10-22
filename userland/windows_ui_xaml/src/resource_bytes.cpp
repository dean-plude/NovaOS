// NovaOS: where the framework finds the controls' default styles and its
// theme resources (roadmap step 32.4).
//   g_platformResourcesModuleHandle, EnsurePlatformResourceModuleHandle:
//     the framework's resource DLL, Microsoft.UI.Xaml.Resources.19H1.dll,
//     loaded as a data file the first time a resource is asked for, as
//     Microsoft's DLL entry (dxaml/dllsrv/winrt/dllentry.cpp, not built:
//     NovaOS links the framework into its programs) does.  NovaOS keeps
//     the DLL in System32, which the search finds.
//   DXamlCore::GetResourceBytes: a resource of that DLL (Styles.xbf,
//     ThemeResources.xbf, TEXTFILE resources) for the parser, as
//     Microsoft's (DXamlCore.cpp, renamed out by build.py's FILE_RENAMES)
//     finds it, except that it says what the bytes are.  On Windows they
//     are XBF, which the XAML compiler makes; NovaOS builds its DLL from
//     generic.xaml without that compiler, Windows-only, so they are the
//     XAML itself, as text (userland/microsoft_ui_xaml_resources_19h1),
//     and the parser reads them as it reads a page.  A resource that is XBF
//     (it starts "XBF") is handed over as XBF, as on Windows.
#include "precomp.h"
#include <ParserAPI.h>

HMODULE g_platformResourcesModuleHandle = nullptr;

void EnsurePlatformResourceModuleHandle()
{
    if (g_platformResourcesModuleHandle != nullptr)
    {
        return;
    }

    static SRWLOCK s_resourceModuleInitLock = SRWLOCK_INIT;
    auto guard = wil::AcquireSRWLockExclusive(&s_resourceModuleInitLock);

    if (g_platformResourcesModuleHandle != nullptr)
    {
        return;
    }

    g_platformResourcesModuleHandle = LoadLibraryExW(
        L"Microsoft.UI.Xaml.Resources.19H1.dll",
        nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (g_platformResourcesModuleHandle == nullptr)
    {
        // (as Microsoft's: XAML cannot run without its resources)
        IFCFAILFAST(E_UNEXPECTED);
    }
}

_Check_return_ HRESULT DirectUI::DXamlCore::GetResourceBytes(
    _In_z_ const WCHAR* pszResourceName,
    _Out_ Parser::XamlBuffer* pBuffer)
{
    EnsurePlatformResourceModuleHandle();

    HRSRC rc = FindResourceW(g_platformResourcesModuleHandle, pszResourceName, MAKEINTRESOURCEW(TEXTFILE));
    if (!rc)
    {
        IFCFAILFAST(HRESULT_FROM_WIN32(GetLastError()));
    }

    DWORD cb = SizeofResource(g_platformResourcesModuleHandle, rc);
    if (0 == cb)
    {
        IFCFAILFAST(HRESULT_FROM_WIN32(GetLastError()));
    }

    HGLOBAL data = LoadResource(g_platformResourcesModuleHandle, rc);
    if (!data)
    {
        IFCFAILFAST(HRESULT_FROM_WIN32(GetLastError()));
    }

    Parser::XamlBuffer result;
    result.m_count = cb;
    result.m_buffer = static_cast<const XUINT8*>(LockResource(data));
    result.m_bufferType = cb >= 3 && !memcmp(result.m_buffer, "XBF", 3) ?
        Parser::XamlBufferType::MemoryMappedResource : Parser::XamlBufferType::Text;
    *pBuffer = result;

    return S_OK;
}
