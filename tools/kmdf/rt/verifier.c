/*
 * verifier.c — the Driver Verifier extension routines (the WDK's
 * VerifierExt library), which the framework calls to ask whether
 * Verifier rule classes are on and to check a pool type is
 * non-executable: NovaOS has no Driver Verifier, so no rule class is on
 * and there is nothing to report
 */
#include <ntddk.h>

BOOLEAN VfIsRuleClassEnabled(ULONG RuleClassID)
{
    UNREFERENCED_PARAMETER(RuleClassID);
    return FALSE;
}

VOID VfCheckNxPoolType(POOL_TYPE PoolType, PVOID CallingAddress, ULONG PoolTag)
{
    UNREFERENCED_PARAMETER(PoolType);
    UNREFERENCED_PARAMETER(CallingAddress);
    UNREFERENCED_PARAMETER(PoolTag);
}
