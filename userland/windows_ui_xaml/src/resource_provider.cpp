// NovaOS: the platform's resource provider, which the control layer's
// resource manager (control/common/shared/ResourceManager.cpp) resolves
// ms-appx and ms-resource URIs through (the parser asks it whether a XAML
// resource, the framework's theme resources among them, may be cached).
// Microsoft's (plat/win/desktop/PlatformServices.cpp) tries MRT Core's
// ResourceManager first (components/mrt's ModernResourceProvider) and
// falls back to the provider over the app's directory,
// CommonResourceProvider, when MRT cannot be used.  MRT Core's Windows
// Runtime classes are not served on NovaOS yet (MRM.dll's resources.pri
// reader, step 32.6, has no ResourceManager class over it), so this makes
// the fallback, as Microsoft's does when they are missing.  Microsoft's
// CommonResourceProvider.cpp is compiled here, after the headers its own
// project's precompiled header (plat/win/desktop's, not built) gives it.
//
// A packaged UWP app keeps its compiled XAML (App.xbf, MainPage.xbf ...)
// inside its resources.pri, not as files: its App.xaml is the framework's
// ms-resource:///Files/app.xaml, which the XAML loader asks for as
// app.xbf.  So a ms-resource file the app's folder does not have is looked
// up in the package's resources.pri, through MRM.dll (MRT Core, the same
// reader Windows' MRT is), as an embedded candidate held in memory, as
// Microsoft's ModernResourceProvider answers it.
#include "precomp.h"
#include <CommonResourceProvider.h>
#include <BasePALResource.h>
#include <FilePathResource.h>
#include <MsResourceHelpers.h>
#include <UriXStringGetters.h>
#include <winpal.h>
#include <winstream.h>
#include <MuiUdk.h>
#include "../../../third_party/microsoft-ui-xaml/dxaml/xcp/plat/win/desktop/CommonResourceProvider.cpp"

// CBasePALResource, the resource every other one derives from (its
// source, components/mrt's BasePALResource.cpp, is not in Microsoft's
// published tree): a resource URI, which is also its physical URI (its
// cache key), local when its scheme is ms-resource, ms-appx or
// ms-appdata; it loads nothing and has no file or stream of its own
_Check_return_ HRESULT CBasePALResource::Create(_In_ IPALUri *pResourceUri, _Outptr_ CBasePALResource **ppResource)
{
    *ppResource = new CBasePALResource(pResourceUri);
    return S_OK;
}

_Check_return_ HRESULT CBasePALResource::Create(_In_ IPALUri *pResourceUri, _Outptr_ IPALResource **ppResource)
{
    *ppResource = new CBasePALResource(pResourceUri);
    return S_OK;
}

_Check_return_ HRESULT CBasePALResource::IsLocalResourceUri(_In_ IPALUri *pUri, _Out_ bool *pfLocal)
{
    xstring_ptr scheme;
    *pfLocal = false;
    IFC_RETURN(UriXStringGetters::GetScheme(pUri, &scheme));
    *pfLocal = scheme.Equals(MsUriHelpers::GetResourceScheme(), xstrCompareCaseInsensitive) ||
               scheme.Equals(MsUriHelpers::GetAppxScheme(), xstrCompareCaseInsensitive) ||
               scheme.Equals(MsUriHelpers::GetAppDataScheme(), xstrCompareCaseInsensitive);
    return S_OK;
}

_Check_return_ HRESULT CBasePALResource::Load(_Outptr_ IPALMemory **ppMemory)
{
    *ppMemory = nullptr;
    return E_NOTIMPL;
}

const WCHAR *CBasePALResource::ToString()
{
    if (m_strToString.IsNull() && m_pResourceUri)
        IGNOREHR(m_pResourceUri->GetCanonical(&m_strToString));
    return m_strToString.GetBuffer();
}

IPALUri *CBasePALResource::GetResourceUriNoRef() { return m_pResourceUri; }
IPALUri *CBasePALResource::GetPhysicalResourceUriNoRef() { return m_pResourceUri; }

_Check_return_ HRESULT CBasePALResource::IsLocal(_Out_ bool *pfLocal)
{
    return IsLocalResourceUri(m_pResourceUri, pfLocal);
}

_Check_return_ HRESULT CBasePALResource::Exists(_Out_ bool *pfExists)
{
    *pfExists = false;
    return S_OK;
}

_Check_return_ HRESULT CBasePALResource::TryGetFilePath(_Out_ xstring_ptr *pstrFilePath)
{
    pstrFilePath->Reset();
    return S_OK;
}

_Check_return_ HRESULT CBasePALResource::TryGetRawStream(const PALResources::RawStreamType, _Outptr_result_maybenull_ void **ppStream)
{
    *ppStream = nullptr;
    return S_OK;
}

XUINT32 CBasePALResource::GetScalePercentage() { return 0; }

// CBufferMemory (win/inc/winpal.h), the PAL's memory over a buffer, which
// it frees (new[]'d, as the parser's XBF writer makes it) when it owns it
CBufferMemory::CBufferMemory(_In_reads_(cBuffer) XUINT8 *pBuffer, _In_ XUINT32 cBuffer, _In_ bool fOwnsBuffer)
    : m_cBuffer(cBuffer), m_pBuffer(pBuffer), m_fOwnsBuffer(fOwnsBuffer)
{
}

CBufferMemory::~CBufferMemory()
{
    if (m_fOwnsBuffer)
        delete[] m_pBuffer;
}

// The PAL's data stream over a buffer (plat/win/common/winstream.h; its
// source, like CBasePALResource's, is not in Microsoft's published tree),
// which the XBF reader reads a compiled XAML file's node and metadata
// sections through (CWindowsServices::CreateIPALStreamFromIPALMemory):
// here only a buffer over an IPALMemory, read-only, and the seekable
// stream over a buffer
namespace {
class CPalMemoryStreamBuffer final : public CWinDataStreamBuffer
{
public:
    explicit CPalMemoryStreamBuffer(_In_ IPALMemory *memory) : m_memory(memory) { SetFastSeekSupport(true); }
    XUINT32 AddRef() override { return static_cast<XUINT32>(InterlockedIncrement(&m_ref)); }
    XUINT32 Release() override
    {
        XUINT32 r = static_cast<XUINT32>(InterlockedDecrement(&m_ref));
        if (!r)
            delete this;
        return r;
    }
    _Check_return_ HRESULT Write(_In_reads_bytes_(cb) void const *, XUINT32 cb, XUINT32, _In_opt_ IStream *) override
    {
        return cb ? E_ACCESSDENIED : S_OK;
    }
    _Check_return_ HRESULT Read(_Out_writes_bytes_(cb) void *pv, XUINT32 cbOffset, XUINT32 cb, _Out_opt_ XUINT32 *pcbRead) override
    {
        XUINT32 size = m_memory->GetSize();
        XUINT32 n = cbOffset >= size ? 0 : (cb < size - cbOffset ? cb : size - cbOffset);
        memcpy(pv, static_cast<const XUINT8 *>(m_memory->GetAddress()) + cbOffset, n);
        if (pcbRead)
            *pcbRead = n;
        return S_OK;
    }
    _Check_return_ HRESULT SetSize(XUINT32) override { return E_ACCESSDENIED; }
    XUINT32 Length() override { return m_memory->GetSize(); }
    XUINT32 Size() override { return m_memory->GetSize(); }
    XUINT32 GetOffset() override { return 0; }
    void SetFinalDownloadSize(_In_ XUINT32) override {}

private:
    ~CPalMemoryStreamBuffer() override = default;

    LONG m_ref = 1;
    xref_ptr<IPALMemory> m_memory;
};
} // namespace

HRESULT CWinDataStreamBuffer::CreateFromIPalMemory(_In_ IPALMemory *pPalMemory, _Outptr_ CWinDataStreamBuffer **ppWinBuffer)
{
    *ppWinBuffer = new CPalMemoryStreamBuffer(pPalMemory);
    return S_OK;
}

_Check_return_ HRESULT CWinDataStreamBuffer::Write(_In_reads_bytes_(cb) const void *pv, XUINT32 cb, XINT32 offset)
{
    return Write(pv, cb, static_cast<XUINT32>(offset), nullptr);
}

_Check_return_ HRESULT CWinDataStreamBuffer::CreateStream(_Out_ IPALStream **ppStream)
{
    CWinDataStream *stream = nullptr;
    IFC_RETURN(CWinDataStream::Create(this, &stream));
    *ppStream = stream;
    return S_OK;
}

CWinDataStream::CWinDataStream() : m_pBuffer(nullptr), m_cRef(1), m_cursor(0), m_capabilities(0) {}

CWinDataStream::~CWinDataStream()
{
    ReleaseInterface(m_pBuffer);
}

HRESULT CWinDataStream::Create(_In_opt_ CWinDataStreamBuffer *pDataBuffer, _Outptr_result_maybenull_ CWinDataStream **ppWinStream)
{
    CWinDataStream *stream = new CWinDataStream();
    stream->m_pBuffer = pDataBuffer;
    AddRefInterface(pDataBuffer);
    stream->m_capabilities = PALSTREAM_CAPS_READABLE | (pDataBuffer && pDataBuffer->SupportFastSeek() ? PALSTREAM_CAPS_SEEKABLE : 0);
    *ppWinStream = stream;
    return S_OK;
}

XUINT32 CWinDataStream::AddRef() { return static_cast<XUINT32>(InterlockedIncrement(reinterpret_cast<volatile LONG *>(&m_cRef))); }

XUINT32 CWinDataStream::Release()
{
    XUINT32 r = static_cast<XUINT32>(InterlockedDecrement(reinterpret_cast<volatile LONG *>(&m_cRef)));
    if (!r)
        delete this;
    return r;
}

_Check_return_ HRESULT CWinDataStream::Clone(_Outptr_result_maybenull_ IPALStream **ppNewStream)
{
    CWinDataStream *stream = nullptr;
    IFC_RETURN(Create(m_pBuffer, &stream));
    stream->m_cursor = m_cursor;
    *ppNewStream = stream;
    return S_OK;
}

_Check_return_ HRESULT CWinDataStream::Read(_Out_writes_bytes_(cb) void *pv, XUINT32 cb, _Out_opt_ XUINT32 *pcbRead)
{
    XUINT32 n = 0;
    if (pcbRead)
        *pcbRead = 0;
    if (!m_pBuffer)
        return E_UNEXPECTED;
    IFC_RETURN(m_pBuffer->Read(pv, m_cursor, cb, &n));
    m_cursor += n;
    if (pcbRead)
        *pcbRead = n;
    return S_OK;
}

_Check_return_ HRESULT CWinDataStream::Seek(XINT64 qwMove, PALSeekOrigin eSeekOrigin, _Out_opt_ XUINT64 *pqwNewPosition)
{
    if (!m_pBuffer)
        return E_UNEXPECTED;
    XINT64 base = eSeekOrigin == SeekOriginCurrent ? m_cursor : eSeekOrigin == SeekOriginEnd ? m_pBuffer->Length() : 0;
    XINT64 to = base + qwMove;
    if (to < 0 || to > static_cast<XINT64>(m_pBuffer->Length()))
        return E_INVALIDARG;
    m_cursor = static_cast<XUINT32>(to);
    if (pqwNewPosition)
        *pqwNewPosition = m_cursor;
    return S_OK;
}

_Check_return_ HRESULT CWinDataStream::SetSize(XUINT64 qwNewSize)
{
    return m_pBuffer ? m_pBuffer->SetSize(static_cast<XUINT32>(qwNewSize)) : E_UNEXPECTED;
}

_Check_return_ HRESULT CWinDataStream::GetSize(_Out_opt_ XUINT64 *pqwSize)
{
    if (pqwSize)
        *pqwSize = m_pBuffer ? m_pBuffer->Length() : 0;
    return S_OK;
}

_Check_return_ HRESULT CWinDataStream::GetOffset(_Out_ XUINT64 *pqOffset)
{
    *pqOffset = m_pBuffer ? m_pBuffer->GetOffset() : 0;
    return S_OK;
}

_Check_return_ HRESULT CWinDataStream::GetPosition(_Out_ XUINT64 *pqPosition)
{
    *pqPosition = m_cursor;
    return S_OK;
}

_Check_return_ HRESULT CWinDataStream::Write(_In_reads_bytes_(cb) const void *pv, XUINT32 cb, XINT32 offset, _Out_opt_ XUINT32 *pcbWritten)
{
    if (pcbWritten)
        *pcbWritten = 0;
    if (!m_pBuffer)
        return E_UNEXPECTED;
    IFC_RETURN(m_pBuffer->Write(pv, cb, static_cast<XUINT32>(offset), nullptr));
    if (pcbWritten)
        *pcbWritten = cb;
    return S_OK;
}

namespace {

// MRM.dll's exports this uses (third_party/mrtcore/mrt/Core/src/MRM.h)
struct MrmData { UINT32 size; void *data; };
typedef HRESULT (STDAPICALLTYPE *MrmCreateResourceManagerFn)(PCWSTR, void **);
typedef HRESULT (STDAPICALLTYPE *MrmLoadEmbeddedFromUriFn)(void *, void *, PCWSTR, MrmData *);
typedef void (STDAPICALLTYPE *MrmFreeResourceFn)(void *);
typedef LONG (WINAPI *GetCurrentPackageInfoFn)(UINT32, UINT32 *, BYTE *, UINT32 *);
// appmodel.h's PACKAGE_INFO (with its PACKAGE_ID's fields in it)
struct NovaPackageInfo
{
    UINT32 reserved, flags;
    PWSTR path, packageFullName, packageFamilyName;
    UINT32 idReserved, processorArchitecture;
    UINT64 version;
    PWSTR name, publisher, resourceId, publisherId;
};

// a candidate's bytes, copied out of MRM.dll's buffer
class EmbeddedMemory final : public IPALMemory
{
public:
    EmbeddedMemory(_In_reads_(size) const void *data, unsigned int size) : m_data(new unsigned char[size ? size : 1]), m_size(size)
    {
        memcpy(m_data, data, size);
    }
    unsigned int AddRef() const override { return static_cast<unsigned int>(InterlockedIncrement(&m_ref)); }
    unsigned int Release() const override
    {
        unsigned int r = static_cast<unsigned int>(InterlockedDecrement(&m_ref));
        if (!r)
            delete this;
        return r;
    }
    unsigned int GetSize() const override { return m_size; }
    void *GetAddress() const override { return m_data; }

private:
    ~EmbeddedMemory() { delete[] m_data; }

    mutable LONG m_ref = 1;
    unsigned char *m_data;
    unsigned int m_size;
};

// an embedded resources.pri candidate: its bytes
class CEmbeddedResource final : public CBasePALResource
{
public:
    CEmbeddedResource(_In_ IPALUri *uri, _In_ IPALMemory *memory) : CBasePALResource(uri), m_memory(memory) {}
    _Check_return_ HRESULT Load(_Outptr_ IPALMemory **ppMemory) override
    {
        m_memory.CopyTo(ppMemory);
        return S_OK;
    }
    const WCHAR *ToString() override { return L"(resources.pri)"; }
    _Check_return_ HRESULT Exists(_Out_ bool *pfExists) override
    {
        *pfExists = true;
        return S_OK;
    }

private:
    xref_ptr<IPALMemory> m_memory;
};

// the app's folder's files (CommonResourceProvider's), then its package's
// resources.pri's embedded files
class NovaResourceProvider final : public IPALResourceProvider, private CReferenceCount
{
public:
    explicit NovaResourceProvider(_In_ CommonResourceProvider *files) : m_files(files) {}
    ~NovaResourceProvider() override
    {
        ReleaseInterface(m_files);
    }

    FORWARD_ADDREF_RELEASE(CReferenceCount);

    _Check_return_ HRESULT TryGetLocalResource(_In_ IPALUri *pResourceUri, _Outptr_result_maybenull_ IPALResource **ppResource) override
    {
        IFC_RETURN(m_files->TryGetLocalResource(pResourceUri, ppResource));
        if (*ppResource)
            return S_OK;
        xstring_ptr map, path;
        bool hadPath = false;
        if (FAILED(MsUriHelpers::CrackMsResourceUri(pResourceUri, &map, nullptr, &path, &hadPath)) || !hadPath ||
            path.IsNullOrEmpty())
            return S_OK;
        void *manager = Manager(map);
        if (!manager)
            return S_OK;
        std::wstring uri = L"ms-resource:///Files/";
        uri.append(path.GetBuffer(), path.GetCount());
        for (auto &c : uri)
            if (c == L'\\')
                c = L'/';
        MrmData data = {};
        if (FAILED(m_load(manager, nullptr, uri.c_str(), &data)) || !data.data)
            return S_OK;
        xref_ptr<IPALMemory> memory;
        memory.attach(new EmbeddedMemory(data.data, data.size));
        m_free(data.data);
        *ppResource = new CEmbeddedResource(pResourceUri, memory.get());
        return S_OK;
    }
    _Check_return_ HRESULT GetString(_In_ const xstring_ptr_view &key, _Out_ xstring_ptr *pstrString) override
    {
        return m_files->GetString(key, pstrString);
    }
    _Check_return_ HRESULT GetPropertyBag(_In_ const IPALUri *pUri, _Out_ PropertyBag &propertyBag) noexcept override
    {
        return m_files->GetPropertyBag(pUri, propertyBag);
    }
    void DetachEvents() override { m_files->DetachEvents(); }
    _Check_return_ HRESULT SetScaleFactor(XUINT32 ulScaleFactor) override { return m_files->SetScaleFactor(ulScaleFactor); }
    _Check_return_ HRESULT NotifyThemeChanged() override { return m_files->NotifyThemeChanged(); }
    _Check_return_ HRESULT SetProcessMUILanguages() override { return m_files->SetProcessMUILanguages(); }

private:
    // MRM.dll's resource manager over the resources.pri of the package
    // whose resource map @map names (the package's name: an app's own
    // resources, or a framework package's, as WinUI 2's themes are
    // ms-appx://Microsoft.UI.Xaml.2.8/...; no map: the app's), from the
    // process's package graph (none for a program outside a package: its
    // files are on disk)
    void *Manager(_In_ const xstring_ptr &map)
    {
        if (!m_tried)
            Open();
        for (auto &pkg : m_packages)
            if (map.IsNullOrEmpty() || _wcsicmp(pkg.name.c_str(), map.GetBuffer()) == 0) {
                if (!pkg.tried) {
                    pkg.tried = true;
                    std::wstring pri = pkg.path + L"\\resources.pri";
                    if (FAILED(m_create(pri.c_str(), &pkg.manager)))
                        pkg.manager = nullptr;
                }
                return pkg.manager;
            }
        return nullptr;
    }

    void Open()
    {
        m_tried = true;
        HMODULE mrm = LoadLibraryW(L"MRM.dll");
        auto info = reinterpret_cast<GetCurrentPackageInfoFn>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentPackageInfo"));
        if (!mrm || !info)
            return;
        m_create = reinterpret_cast<MrmCreateResourceManagerFn>(GetProcAddress(mrm, "MrmCreateResourceManager"));
        m_load = reinterpret_cast<MrmLoadEmbeddedFromUriFn>(GetProcAddress(mrm, "MrmLoadEmbeddedResourceFromResourceUri"));
        m_free = reinterpret_cast<MrmFreeResourceFn>(GetProcAddress(mrm, "MrmFreeResource"));
        UINT32 len = 0, count = 0;
        if (!m_create || !m_load || !m_free || info(0, &len, nullptr, &count) != ERROR_INSUFFICIENT_BUFFER)
            return;
        std::vector<BYTE> buf(len);
        if (info(0, &len, buf.data(), &count) != ERROR_SUCCESS)
            return;
        auto pi = reinterpret_cast<const NovaPackageInfo *>(buf.data());
        for (UINT32 i = 0; i < count; i++)
            if (pi[i].path && pi[i].name)
                m_packages.push_back({pi[i].name, pi[i].path});
    }

    struct Package
    {
        std::wstring name, path;
        bool tried = false;
        void *manager = nullptr;
    };

    CommonResourceProvider *m_files;
    bool m_tried = false;
    std::vector<Package> m_packages;
    MrmCreateResourceManagerFn m_create = nullptr;
    MrmLoadEmbeddedFromUriFn m_load = nullptr;
    MrmFreeResourceFn m_free = nullptr;
};

} // namespace

_Check_return_ HRESULT CWindowsServices::CreateResourceProvider(
    _In_ CCoreServices *pCore,
    _Outptr_ IPALResourceProvider **ppResourceProvider)
{
    (void)pCore;
    CommonResourceProvider *provider = nullptr;
    IFC_RETURN(CommonResourceProvider::Create(&provider));
    *ppResourceProvider = new NovaResourceProvider(provider);
    return S_OK;
}
