/* roapi.h — Windows Runtime activation, in ole32 (Windows: combase) */
#pragma once
#include <winstring.h>

_NOVA_BEGIN

typedef enum RO_INIT_TYPE { RO_INIT_SINGLETHREADED = 0, RO_INIT_MULTITHREADED = 1 } RO_INIT_TYPE;
#ifndef __EventRegistrationToken_DEFINED
#define __EventRegistrationToken_DEFINED
typedef struct EventRegistrationToken { INT64 value; } EventRegistrationToken;
#endif
typedef enum TrustLevel { BaseTrust = 0, PartialTrust = 1, FullTrust = 2 } TrustLevel;

WINOLEAPI_(HRESULT) RoInitialize(RO_INIT_TYPE type);
WINOLEAPI_(void) RoUninitialize(void);
WINOLEAPI_(HRESULT) RoGetActivationFactory(HSTRING cls, REFIID iid, void **factory);
WINOLEAPI_(HRESULT) RoActivateInstance(HSTRING cls, void **instance);

/* the in-process server's export (DllGetActivationFactory) */
typedef HRESULT (WINAPI *PFNGETACTIVATIONFACTORY)(HSTRING cls, void **factory);

_NOVA_END
