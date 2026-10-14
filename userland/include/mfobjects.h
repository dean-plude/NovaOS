/*
 * mfobjects.h — Media Foundation's basic objects: attribute stores, media
 * types, buffers, samples, activation objects (userland/mfplat)
 */
#pragma once
#include <objbase.h>
#include <oleauto.h>

/* (NovaOS's Media Foundation DLLs define NOVA_MF_BUILD: they export these) */
#ifdef NOVA_MF_BUILD
#define NOVA_MF_API __declspec(dllexport)
#else
#define NOVA_MF_API __declspec(dllimport)
#endif

#ifndef NOVA_PROPVARIANT_DEFINED
#define NOVA_PROPVARIANT_DEFINED
typedef struct tagPROPVARIANT {
    VARTYPE vt;
    WORD    wReserved1, wReserved2, wReserved3;
    union {
        CHAR cVal; UCHAR bVal; SHORT iVal; USHORT uiVal; LONG lVal; ULONG ulVal; INT intVal; UINT uintVal;
        LARGE_INTEGER hVal; ULARGE_INTEGER uhVal; float fltVal; double dblVal;
        CLSID *puuid; LPSTR pszVal; LPWSTR pwszVal; IUnknown *punkVal;
        struct { ULONG cElems; UCHAR *pElems; } caub;
        struct { ULONG cbSize; BYTE *pBlobData; } blob;
    };
} PROPVARIANT, *LPPROPVARIANT;
#ifdef __cplusplus
typedef const PROPVARIANT &REFPROPVARIANT;
#else
typedef const PROPVARIANT *REFPROPVARIANT;
#endif
#endif
WINOLEAPI PropVariantClear(PROPVARIANT *pv);
WINOLEAPI PropVariantCopy(PROPVARIANT *dst, const PROPVARIANT *src);

typedef enum {
    MF_ATTRIBUTE_UINT32 = VT_UI4, MF_ATTRIBUTE_UINT64 = VT_UI8, MF_ATTRIBUTE_DOUBLE = VT_R8,
    MF_ATTRIBUTE_GUID = VT_CLSID, MF_ATTRIBUTE_STRING = VT_LPWSTR, MF_ATTRIBUTE_BLOB = VT_VECTOR | VT_UI1,
    MF_ATTRIBUTE_IUNKNOWN = VT_UNKNOWN
} MF_ATTRIBUTE_TYPE;
typedef enum {
    MF_ATTRIBUTES_MATCH_OUR_ITEMS, MF_ATTRIBUTES_MATCH_THEIR_ITEMS, MF_ATTRIBUTES_MATCH_ALL_ITEMS,
    MF_ATTRIBUTES_MATCH_INTERSECTION, MF_ATTRIBUTES_MATCH_SMALLER
} MF_ATTRIBUTES_MATCH_TYPE;

#define MF_MEDIATYPE_EQUAL_MAJOR_TYPES     0x1
#define MF_MEDIATYPE_EQUAL_FORMAT_TYPES    0x2
#define MF_MEDIATYPE_EQUAL_FORMAT_DATA     0x4
#define MF_MEDIATYPE_EQUAL_FORMAT_USER_DATA 0x8

DEFINE_GUID(IID_IMFAttributes,          0x2CD2D921, 0xC447, 0x44A7, 0xA1, 0x3C, 0x4A, 0xDA, 0xBF, 0xC2, 0x47, 0xE3);
DEFINE_GUID(IID_IMFMediaType,           0x44AE0FA8, 0xEA31, 0x4109, 0x8D, 0x2E, 0x4C, 0xAE, 0x49, 0x97, 0xC5, 0x55);
DEFINE_GUID(IID_IMFMediaBuffer,         0x045FA593, 0x8799, 0x42B8, 0xBC, 0x8D, 0x89, 0x68, 0xC6, 0x45, 0x35, 0x07);
DEFINE_GUID(IID_IMF2DBuffer,            0x7DC9D5F9, 0x9ED9, 0x44EC, 0x9B, 0xBF, 0x06, 0x00, 0xBB, 0x58, 0x9F, 0xBB);
DEFINE_GUID(IID_IMFSample,              0xC40A00F2, 0xB93A, 0x4D80, 0xAE, 0x8C, 0x5A, 0x1C, 0x63, 0x4F, 0x58, 0xE4);
DEFINE_GUID(IID_IMFActivate,            0x7FEE9E9A, 0x4A89, 0x47A6, 0x89, 0x9C, 0xB6, 0xA5, 0x3A, 0x70, 0xFB, 0x67);
DEFINE_GUID(IID_IMFMediaEventGenerator, 0x2CD0BD52, 0xBCD5, 0x4B89, 0xB6, 0x2C, 0xEA, 0xDC, 0x0C, 0x03, 0x1E, 0x7D);

typedef interface IMFMediaEvent IMFMediaEvent;
typedef interface IMFAsyncCallback IMFAsyncCallback;
typedef interface IMFAsyncResult IMFAsyncResult;

/* ---- IMFAttributes ---- */
/* (its methods, after IUnknown's: every interface built on it lists them) */
#define NOVA_IMFATTRIBUTES_METHODS \
    STDMETHOD(GetItem)(THIS_ REFGUID key, PROPVARIANT *value) PURE; \
    STDMETHOD(GetItemType)(THIS_ REFGUID key, MF_ATTRIBUTE_TYPE *type) PURE; \
    STDMETHOD(CompareItem)(THIS_ REFGUID key, REFPROPVARIANT value, BOOL *result) PURE; \
    STDMETHOD(Compare)(THIS_ IMFAttributes *theirs, MF_ATTRIBUTES_MATCH_TYPE type, BOOL *result) PURE; \
    STDMETHOD(GetUINT32)(THIS_ REFGUID key, UINT32 *value) PURE; \
    STDMETHOD(GetUINT64)(THIS_ REFGUID key, UINT64 *value) PURE; \
    STDMETHOD(GetDouble)(THIS_ REFGUID key, double *value) PURE; \
    STDMETHOD(GetGUID)(THIS_ REFGUID key, GUID *value) PURE; \
    STDMETHOD(GetStringLength)(THIS_ REFGUID key, UINT32 *length) PURE; \
    STDMETHOD(GetString)(THIS_ REFGUID key, LPWSTR value, UINT32 size, UINT32 *length) PURE; \
    STDMETHOD(GetAllocatedString)(THIS_ REFGUID key, LPWSTR *value, UINT32 *length) PURE; \
    STDMETHOD(GetBlobSize)(THIS_ REFGUID key, UINT32 *size) PURE; \
    STDMETHOD(GetBlob)(THIS_ REFGUID key, UINT8 *buf, UINT32 size, UINT32 *blob_size) PURE; \
    STDMETHOD(GetAllocatedBlob)(THIS_ REFGUID key, UINT8 **buf, UINT32 *size) PURE; \
    STDMETHOD(GetUnknown)(THIS_ REFGUID key, REFIID riid, void **ppv) PURE; \
    STDMETHOD(SetItem)(THIS_ REFGUID key, REFPROPVARIANT value) PURE; \
    STDMETHOD(DeleteItem)(THIS_ REFGUID key) PURE; \
    STDMETHOD(DeleteAllItems)(THIS) PURE; \
    STDMETHOD(SetUINT32)(THIS_ REFGUID key, UINT32 value) PURE; \
    STDMETHOD(SetUINT64)(THIS_ REFGUID key, UINT64 value) PURE; \
    STDMETHOD(SetDouble)(THIS_ REFGUID key, double value) PURE; \
    STDMETHOD(SetGUID)(THIS_ REFGUID key, REFGUID value) PURE; \
    STDMETHOD(SetString)(THIS_ REFGUID key, LPCWSTR value) PURE; \
    STDMETHOD(SetBlob)(THIS_ REFGUID key, const UINT8 *buf, UINT32 size) PURE; \
    STDMETHOD(SetUnknown)(THIS_ REFGUID key, IUnknown *unknown) PURE; \
    STDMETHOD(LockStore)(THIS) PURE; \
    STDMETHOD(UnlockStore)(THIS) PURE; \
    STDMETHOD(GetCount)(THIS_ UINT32 *count) PURE; \
    STDMETHOD(GetItemByIndex)(THIS_ UINT32 index, GUID *key, PROPVARIANT *value) PURE; \
    STDMETHOD(CopyAllItems)(THIS_ IMFAttributes *dest) PURE;
#define NOVA_IUNKNOWN_METHODS \
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppv) PURE; \
    STDMETHOD_(ULONG, AddRef)(THIS) PURE; \
    STDMETHOD_(ULONG, Release)(THIS) PURE;
#ifdef __cplusplus
#define NOVA_MF_BASE(attrs)
#else
#define NOVA_MF_BASE(attrs) NOVA_IUNKNOWN_METHODS attrs
#endif

#undef INTERFACE
#define INTERFACE IMFAttributes
DECLARE_INTERFACE_(IMFAttributes, IUnknown)
{
    BEGIN_INTERFACE
    NOVA_MF_BASE()
    NOVA_IMFATTRIBUTES_METHODS
    END_INTERFACE
};

/* ---- IMFMediaType ---- */
#undef INTERFACE
#define INTERFACE IMFMediaType
DECLARE_INTERFACE_(IMFMediaType, IMFAttributes)
{
    BEGIN_INTERFACE
    NOVA_MF_BASE(NOVA_IMFATTRIBUTES_METHODS)
    STDMETHOD(GetMajorType)(THIS_ GUID *major) PURE;
    STDMETHOD(IsCompressedFormat)(THIS_ BOOL *compressed) PURE;
    STDMETHOD(IsEqual)(THIS_ IMFMediaType *type, DWORD *flags) PURE;
    STDMETHOD(GetRepresentation)(THIS_ GUID guid, void **representation) PURE;
    STDMETHOD(FreeRepresentation)(THIS_ GUID guid, void *representation) PURE;
    END_INTERFACE
};

/* ---- IMFMediaBuffer ---- */
#undef INTERFACE
#define INTERFACE IMFMediaBuffer
DECLARE_INTERFACE_(IMFMediaBuffer, IUnknown)
{
    BEGIN_INTERFACE
    NOVA_MF_BASE()
    STDMETHOD(Lock)(THIS_ BYTE **data, DWORD *max_length, DWORD *current_length) PURE;
    STDMETHOD(Unlock)(THIS) PURE;
    STDMETHOD(GetCurrentLength)(THIS_ DWORD *length) PURE;
    STDMETHOD(SetCurrentLength)(THIS_ DWORD length) PURE;
    STDMETHOD(GetMaxLength)(THIS_ DWORD *length) PURE;
    END_INTERFACE
};

/* ---- IMF2DBuffer ---- */
#undef INTERFACE
#define INTERFACE IMF2DBuffer
DECLARE_INTERFACE_(IMF2DBuffer, IUnknown)
{
    BEGIN_INTERFACE
    NOVA_MF_BASE()
    STDMETHOD(Lock2D)(THIS_ BYTE **scanline0, LONG *pitch) PURE;
    STDMETHOD(Unlock2D)(THIS) PURE;
    STDMETHOD(GetScanline0AndPitch)(THIS_ BYTE **scanline0, LONG *pitch) PURE;
    STDMETHOD(IsContiguousFormat)(THIS_ BOOL *contiguous) PURE;
    STDMETHOD(GetContiguousLength)(THIS_ DWORD *length) PURE;
    STDMETHOD(ContiguousCopyTo)(THIS_ BYTE *dest, DWORD dest_length) PURE;
    STDMETHOD(ContiguousCopyFrom)(THIS_ const BYTE *src, DWORD src_length) PURE;
    END_INTERFACE
};

/* ---- IMFSample ---- */
#undef INTERFACE
#define INTERFACE IMFSample
DECLARE_INTERFACE_(IMFSample, IMFAttributes)
{
    BEGIN_INTERFACE
    NOVA_MF_BASE(NOVA_IMFATTRIBUTES_METHODS)
    STDMETHOD(GetSampleFlags)(THIS_ DWORD *flags) PURE;
    STDMETHOD(SetSampleFlags)(THIS_ DWORD flags) PURE;
    STDMETHOD(GetSampleTime)(THIS_ LONGLONG *time) PURE;
    STDMETHOD(SetSampleTime)(THIS_ LONGLONG time) PURE;
    STDMETHOD(GetSampleDuration)(THIS_ LONGLONG *duration) PURE;
    STDMETHOD(SetSampleDuration)(THIS_ LONGLONG duration) PURE;
    STDMETHOD(GetBufferCount)(THIS_ DWORD *count) PURE;
    STDMETHOD(GetBufferByIndex)(THIS_ DWORD index, IMFMediaBuffer **buffer) PURE;
    STDMETHOD(ConvertToContiguousBuffer)(THIS_ IMFMediaBuffer **buffer) PURE;
    STDMETHOD(AddBuffer)(THIS_ IMFMediaBuffer *buffer) PURE;
    STDMETHOD(RemoveBufferByIndex)(THIS_ DWORD index) PURE;
    STDMETHOD(RemoveAllBuffers)(THIS) PURE;
    STDMETHOD(GetTotalLength)(THIS_ DWORD *length) PURE;
    STDMETHOD(CopyToBuffer)(THIS_ IMFMediaBuffer *buffer) PURE;
    END_INTERFACE
};

/* ---- IMFActivate ---- */
#undef INTERFACE
#define INTERFACE IMFActivate
DECLARE_INTERFACE_(IMFActivate, IMFAttributes)
{
    BEGIN_INTERFACE
    NOVA_MF_BASE(NOVA_IMFATTRIBUTES_METHODS)
    STDMETHOD(ActivateObject)(THIS_ REFIID riid, void **ppv) PURE;
    STDMETHOD(ShutdownObject)(THIS) PURE;
    STDMETHOD(DetachObject)(THIS) PURE;
    END_INTERFACE
};

/* ---- IMFMediaEventGenerator ---- */
#define NOVA_IMFMEDIAEVENTGENERATOR_METHODS \
    STDMETHOD(GetEvent)(THIS_ DWORD flags, IMFMediaEvent **event) PURE; \
    STDMETHOD(BeginGetEvent)(THIS_ IMFAsyncCallback *callback, IUnknown *state) PURE; \
    STDMETHOD(EndGetEvent)(THIS_ IMFAsyncResult *result, IMFMediaEvent **event) PURE; \
    STDMETHOD(QueueEvent)(THIS_ DWORD type, REFGUID extended_type, HRESULT status, const PROPVARIANT *value) PURE;
#undef INTERFACE
#define INTERFACE IMFMediaEventGenerator
DECLARE_INTERFACE_(IMFMediaEventGenerator, IUnknown)
{
    BEGIN_INTERFACE
    NOVA_MF_BASE()
    NOVA_IMFMEDIAEVENTGENERATOR_METHODS
    END_INTERFACE
};
#undef INTERFACE

