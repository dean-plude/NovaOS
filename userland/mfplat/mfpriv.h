/*
 * mfpriv.h — what mfplat.dll's parts share (mfplat.c: attribute stores,
 * media types, buffers, samples; camera.c: capture devices; reader.c:
 * the source reader; mjpeg.c: MJPEG frames decoded)
 */
#pragma once
#define NOVA_MF_BUILD
#include <windows.h>
#include <string.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <novacam.h>

#define MFEXPORT __declspec(dllexport)

WINBASEAPI BOOL WINAPI DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD in_n, LPVOID out, DWORD out_n,
                                       LPDWORD got, LPOVERLAPPED ov);
#ifndef ERROR_BUSY
#define ERROR_BUSY 170
#endif

/* ---- attribute stores (mfplat.c) ----
 * Every Media Foundation object built on IMFAttributes starts with this
 * header, so one set of IMFAttributes methods serves them all. */
typedef struct {
    GUID        key;
    PROPVARIANT value;
} AttrItem;

typedef struct {
    const void      *vtbl;
    LONG             ref;
    CRITICAL_SECTION cs;
    AttrItem        *items;
    UINT32           count, cap;
} AttrObj;

void    attr_init(AttrObj *a, const void *vtbl);
void    attr_free(AttrObj *a);
HRESULT attr_copy_into(AttrObj *src, IMFAttributes *dst);

/* The 32 IMFAttributes slots (with IUnknown's AddRef) of a vtable for a
 * type whose QueryInterface and Release are @qi and @rel */
#define ATTR_VTBL(qi, rel) \
    .QueryInterface = (void *)(qi), .AddRef = (void *)attr_AddRef, .Release = (void *)(rel), \
    .GetItem = (void *)attr_GetItem, .GetItemType = (void *)attr_GetItemType, .CompareItem = (void *)attr_CompareItem, \
    .Compare = (void *)attr_Compare, .GetUINT32 = (void *)attr_GetUINT32, .GetUINT64 = (void *)attr_GetUINT64, \
    .GetDouble = (void *)attr_GetDouble, .GetGUID = (void *)attr_GetGUID, .GetStringLength = (void *)attr_GetStringLength, \
    .GetString = (void *)attr_GetString, .GetAllocatedString = (void *)attr_GetAllocatedString, \
    .GetBlobSize = (void *)attr_GetBlobSize, .GetBlob = (void *)attr_GetBlob, .GetAllocatedBlob = (void *)attr_GetAllocatedBlob, \
    .GetUnknown = (void *)attr_GetUnknown, .SetItem = (void *)attr_SetItem, .DeleteItem = (void *)attr_DeleteItem, \
    .DeleteAllItems = (void *)attr_DeleteAllItems, .SetUINT32 = (void *)attr_SetUINT32, .SetUINT64 = (void *)attr_SetUINT64, \
    .SetDouble = (void *)attr_SetDouble, .SetGUID = (void *)attr_SetGUID, .SetString = (void *)attr_SetString, \
    .SetBlob = (void *)attr_SetBlob, .SetUnknown = (void *)attr_SetUnknown, .LockStore = (void *)attr_LockStore, \
    .UnlockStore = (void *)attr_UnlockStore, .GetCount = (void *)attr_GetCount, .GetItemByIndex = (void *)attr_GetItemByIndex, \
    .CopyAllItems = (void *)attr_CopyAllItems

#define A IMFAttributes *This
ULONG   STDMETHODCALLTYPE attr_AddRef(A);
HRESULT STDMETHODCALLTYPE attr_GetItem(A, REFGUID key, PROPVARIANT *v);
HRESULT STDMETHODCALLTYPE attr_GetItemType(A, REFGUID key, MF_ATTRIBUTE_TYPE *t);
HRESULT STDMETHODCALLTYPE attr_CompareItem(A, REFGUID key, REFPROPVARIANT v, BOOL *r);
HRESULT STDMETHODCALLTYPE attr_Compare(A, IMFAttributes *theirs, MF_ATTRIBUTES_MATCH_TYPE t, BOOL *r);
HRESULT STDMETHODCALLTYPE attr_GetUINT32(A, REFGUID key, UINT32 *v);
HRESULT STDMETHODCALLTYPE attr_GetUINT64(A, REFGUID key, UINT64 *v);
HRESULT STDMETHODCALLTYPE attr_GetDouble(A, REFGUID key, double *v);
HRESULT STDMETHODCALLTYPE attr_GetGUID(A, REFGUID key, GUID *v);
HRESULT STDMETHODCALLTYPE attr_GetStringLength(A, REFGUID key, UINT32 *n);
HRESULT STDMETHODCALLTYPE attr_GetString(A, REFGUID key, LPWSTR v, UINT32 size, UINT32 *n);
HRESULT STDMETHODCALLTYPE attr_GetAllocatedString(A, REFGUID key, LPWSTR *v, UINT32 *n);
HRESULT STDMETHODCALLTYPE attr_GetBlobSize(A, REFGUID key, UINT32 *n);
HRESULT STDMETHODCALLTYPE attr_GetBlob(A, REFGUID key, UINT8 *buf, UINT32 size, UINT32 *n);
HRESULT STDMETHODCALLTYPE attr_GetAllocatedBlob(A, REFGUID key, UINT8 **buf, UINT32 *n);
HRESULT STDMETHODCALLTYPE attr_GetUnknown(A, REFGUID key, REFIID riid, void **ppv);
HRESULT STDMETHODCALLTYPE attr_SetItem(A, REFGUID key, REFPROPVARIANT v);
HRESULT STDMETHODCALLTYPE attr_DeleteItem(A, REFGUID key);
HRESULT STDMETHODCALLTYPE attr_DeleteAllItems(A);
HRESULT STDMETHODCALLTYPE attr_SetUINT32(A, REFGUID key, UINT32 v);
HRESULT STDMETHODCALLTYPE attr_SetUINT64(A, REFGUID key, UINT64 v);
HRESULT STDMETHODCALLTYPE attr_SetDouble(A, REFGUID key, double v);
HRESULT STDMETHODCALLTYPE attr_SetGUID(A, REFGUID key, REFGUID v);
HRESULT STDMETHODCALLTYPE attr_SetString(A, REFGUID key, LPCWSTR v);
HRESULT STDMETHODCALLTYPE attr_SetBlob(A, REFGUID key, const UINT8 *buf, UINT32 n);
HRESULT STDMETHODCALLTYPE attr_SetUnknown(A, REFGUID key, IUnknown *u);
HRESULT STDMETHODCALLTYPE attr_LockStore(A);
HRESULT STDMETHODCALLTYPE attr_UnlockStore(A);
HRESULT STDMETHODCALLTYPE attr_GetCount(A, UINT32 *n);
HRESULT STDMETHODCALLTYPE attr_GetItemByIndex(A, UINT32 i, GUID *key, PROPVARIANT *v);
HRESULT STDMETHODCALLTYPE attr_CopyAllItems(A, IMFAttributes *dst);
#undef A

/* Objects (mfplat.c) */
HRESULT mf_create_media_type(IMFMediaType **out);
HRESULT mf_create_video_buffer(DWORD length, LONG stride, UINT32 height, IMFMediaBuffer **out);
HRESULT mf_create_sample(IMFSample **out);
HRESULT mf_create_attributes(IMFAttributes **out, UINT32 size);

/* ---- capture devices (camera.c) ---- */
typedef struct Camera Camera;
HRESULT nova_enum_device_sources(IMFAttributes *attrs, IMFActivate ***sources, UINT32 *count);
HRESULT nova_create_device_source(IMFAttributes *attrs, IMFMediaSource **source);
Camera *camera_of(IMFMediaSource *source);            /* NULL: not NovaOS's camera source */
UINT32  camera_mode_count(Camera *c);
const NovaCamMode *camera_mode(Camera *c, UINT32 i);
UINT32  camera_current_mode(Camera *c);
HRESULT camera_set_mode(Camera *c, UINT32 i);
HRESULT camera_native_type(Camera *c, UINT32 i, IMFMediaType **out);
HRESULT camera_start(Camera *c, UINT32 interval);
HRESULT camera_read(Camera *c, BYTE **frame, UINT32 *bytes, UINT32 *seq);   /* the frame stays the camera's */
void    camera_stop(Camera *c);
DWORD   camera_characteristics(Camera *c);
const GUID *camera_subtype(UINT32 fourcc, GUID *buf);

/* ---- mjpeg.c ---- */
/* One MJPEG frame (@len bytes) of @w x @h into @rgb (w x h x 3 bytes,
 * R, G, B); FALSE when it is not one (a damaged frame, another size) */
BOOL mjpeg_decode(const BYTE *jpg, UINT32 len, UINT32 w, UINT32 h, BYTE *rgb);
void    mf_fill_video_type(IMFMediaType *t, const GUID *subtype, UINT32 width, UINT32 height, UINT32 interval,
                           UINT32 min_interval, BOOL compressed);

/* ---- the source reader (reader.c) ---- */
HRESULT nova_create_source_reader(IMFMediaSource *source, IMFAttributes *attrs, IMFSourceReader **reader);

/* GUIDs NovaOS's objects answer that no header names */
DEFINE_GUID(MF_SOURCE_READER_MEDIASOURCE_CHARACTERISTICS, 0x6D23F5C8, 0xC5D7, 0x4A9B, 0x99, 0x71, 0x5D, 0x11, 0xF8, 0xBC, 0xA8, 0x80);
#define MF_E_UNSUPPORTED_SERVICE ((HRESULT)0xC00D36BAL)
#define MF_SOURCE_READER_CURRENT_TYPE_INDEX 0xFFFFFFFF
