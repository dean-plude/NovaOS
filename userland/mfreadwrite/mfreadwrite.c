/*
 * mfreadwrite.dll — the Media Foundation source reader
 * (MFCreateSourceReaderFromMediaSource; the reader is mfplat.dll's
 * reader.c).  Files and URLs cannot be read yet, and there is no sink
 * writer.
 */
#define NOVA_MF_BUILD
#include <mfreadwrite.h>

__declspec(dllimport) HRESULT WINAPI NovaMFCreateSourceReader(IMFMediaSource *source, IMFAttributes *attrs, IMFSourceReader **reader);

HRESULT WINAPI MFCreateSourceReaderFromMediaSource(IMFMediaSource *source, IMFAttributes *attrs,
                                                                        IMFSourceReader **reader)
{
    return NovaMFCreateSourceReader(source, attrs, reader);
}

HRESULT WINAPI MFCreateSourceReaderFromURL(LPCWSTR url, IMFAttributes *attrs, IMFSourceReader **reader)
{
    (void)url; (void)attrs;
    if (reader) *reader = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI MFCreateSourceReaderFromByteStream(IUnknown *stream, IMFAttributes *attrs, IMFSourceReader **reader)
{
    (void)stream; (void)attrs;
    if (reader) *reader = NULL;
    return E_NOTIMPL;
}
