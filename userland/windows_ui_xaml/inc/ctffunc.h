/* NovaOS: ctffunc.h, the Windows SDK's Text Services Framework function
 * header, for what XAML's text boxes use of it (MinGW-w64 has none):
 * IUIManagerEventSink, which a text box gives the input method's UI
 * manager to hear where its candidate window opens
 * (core/native/text/Controls/TextBoxUIManagerEventSink). */
#pragma once
#include <msctf.h>

#ifdef __cplusplus
struct DECLSPEC_UUID("cd91d690-a7e8-4265-9b38-8bb3bbaba7de") IUIManagerEventSink : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE OnWindowOpening(RECT *prcBounds) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnWindowOpened(RECT *prcBounds) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnWindowUpdating(RECT *prcUpdatedBounds) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnWindowUpdated(RECT *prcUpdatedBounds) = 0;
    virtual HRESULT STDMETHODCALLTYPE OnWindowClosing() = 0;
    virtual HRESULT STDMETHODCALLTYPE OnWindowClosed() = 0;
};
static const IID IID_IUIManagerEventSink = {0xcd91d690, 0xa7e8, 0x4265, {0x9b, 0x38, 0x8b, 0xb3, 0xbb, 0xab, 0xa7, 0xde}};
#endif
