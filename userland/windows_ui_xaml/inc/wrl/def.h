/* NovaOS's WRL (see wrl/client.h): definitions every header shares */
#pragma once
#include <windows.h>

#ifndef __WRL_ASSERT__
#define __WRL_ASSERT__(cond) ((void)0)
#endif
#ifndef __WRL_IMPLEMENTS_FTM_BASE__
#define __WRL_IMPLEMENTS_FTM_BASE__(flags) (((flags) & ::Microsoft::WRL::InhibitFtmBase) == 0)
#endif

/* a class nothing derives from (WIL's async helpers) */
#ifndef WrlFinal
#define WrlFinal final
#endif

namespace Microsoft { namespace WRL {
enum RuntimeClassType
{
    WinRt = 0x0001,
    ClassicCom = 0x0002,
    WinRtClassicComMix = WinRt | ClassicCom,
    InhibitWeakReference = 0x0004,
    Delegate = ClassicCom,
    InhibitFtmBase = 0x0008,
    InhibitRoOriginateError = 0x0010
};
}}
