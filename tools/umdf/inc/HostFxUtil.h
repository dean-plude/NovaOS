/* HostFxUtil.h — the UMDF verifier checks (NovaOS stand-in for the
 * internal header, README.md)
 *
 * FX_VERIFY(kind, check): kind is INTERNAL (a framework or host bug) or
 * DRIVER(problem, detail) (the driver broke a rule); check is one of the
 * CHECK_ forms below, true when all is well.  A failed check stops the
 * host process as Windows' does (tools/umdf/rt/verify.cpp says which
 * check failed and where). */
#pragma once

#ifdef __cplusplus
extern "C"
#endif
DECLSPEC_NORETURN void __cdecl NovaUmdfVerifyFailed(const char *Kind, const char *Check, const char *Name,
                                                    const char *File, int Line);

#define CHECK_NOT_NULL(p)  ((p) != NULL)
#define CHECK_NULL(p)      ((p) == NULL)
#define CHECK_QI(hr, p)    (SUCCEEDED(hr) && (p) != NULL)
#define CHECK_HR(hr)       SUCCEEDED(hr)
#define CHECK_HANDLE(h)    ((h) != NULL && (h) != INVALID_HANDLE_VALUE)
#define CHECK_TODO(e)      (e)
#define TRAPMSG(m)         (0 && (m))

#define FX_VERIFY_WITH_NAME(Kind, Check, Name) \
    do { if (!(Check)) NovaUmdfVerifyFailed(#Kind, #Check, (const char *)(Name), __FILE__, __LINE__); } while (0)
#define FX_VERIFY(Kind, Check) \
    do { if (!(Check)) NovaUmdfVerifyFailed(#Kind, #Check, NULL, __FILE__, __LINE__); } while (0)

#define CHECK(Message, e)  (e)
#define VERIFY(e)          (e)
#define FX_VERIFY_BUGCHECK(Code, Message, Name, ...) \
    NovaUmdfVerifyFailed("BugCheck " #Code, Message, (const char *)(Name), __FILE__, __LINE__)

/* A shared-code path that UMDF does not implement (UfxVerifier.h) */
#ifdef __cplusplus
extern "C"
#endif
VOID UfxVerifierTrapNotImpl(VOID);

/* What an HRESULT from the host means as an NTSTATUS for the driver
 * (tools/umdf/rt/verify.cpp) */
#ifdef __cplusplus
class CHostFxUtil {
public:
    static NTSTATUS NtStatusFromHr(HRESULT hr, ULONG DriverMajorVersion, ULONG DriverMinorVersion,
                                   BOOL PreserveCompatibility);
};
#endif
