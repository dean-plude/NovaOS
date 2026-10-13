/*
 * bindhelper.cpp — WdfBindClientHelper: the version check the framework
 * makes when a client driver binds (WDF_LIBRARY_REGISTER_CLIENT calls it
 * first), from the WDK's loader library, which Microsoft did not publish
 *
 * A client's bind information names the KMDF version it was built for
 * (1.minor) and, in its version 2 form, the oldest minor version it can run
 * on.  The major versions must match.  A client built for this framework's
 * minor version or an older one binds as it is.  A client built for a newer
 * one binds when its minimum is not newer than this framework: it is told
 * so (ClientVersionHigherThanFramework), its function count is cut to this
 * framework's, and it is given this framework's structure sizes, which is
 * what its WDF_IS_FUNCTION_AVAILABLE and WDF_STRUCTURE_SIZE read.
 *
 * The user-mode framework (tools/umdf/rt/bindhelper.cpp) builds this file
 * with NOVA_UMDF defined: there the helper has C++ linkage, as UMDF's
 * fxldr.h declares it.
 */
#ifdef NOVA_UMDF
#include "fxmin.hpp"
#include <fxldr.h>
#define BIND_LINKAGE
#else
extern "C" {
#include <ntddk.h>
}
#include "fx.hpp"
#include <fxldr.h>
#define BIND_LINKAGE extern "C"
#endif

/* sizeof each public structure, indexed by WDFSTRUCTENUM (wdffuncenum.h);
 * build_kmdf.py writes the list from that header */
static size_t g_StructSizes[] = {
#define WDF_STRUCT(name) sizeof(name),
#include "wdfstructs.inc"
#undef WDF_STRUCT
};
C_ASSERT(RTL_NUMBER_OF(g_StructSizes) == WDF_STRUCTURE_TABLE_NUM_ENTRIES);

BIND_LINKAGE
NTSTATUS
WdfBindClientHelper(
    _Inout_ PWDF_BIND_INFO BindInfo,
    _In_ WDF_MAJOR_VERSION FxMajorVersion,
    _In_ WDF_MINOR_VERSION FxMinorVersion
    )
{
    PWDF_BIND_INFO2 info2 = NULL;
    WDF_MINOR_VERSION minimum = BindInfo->Version.Minor;

    if (BindInfo->Version.Major != FxMajorVersion) {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    if (BindInfo->Size >= sizeof(WDF_BIND_INFO2)) {
        info2 = (PWDF_BIND_INFO2)BindInfo;
        if (info2->MinimumVersionRequired != NULL && *info2->MinimumVersionRequired != (WDF_MINOR_VERSION)-1) {
            minimum = *info2->MinimumVersionRequired;
        }
    }
    if (BindInfo->Version.Minor <= FxMinorVersion) {
        if (info2 != NULL && info2->ClientVersionHigherThanFramework != NULL) {
            *info2->ClientVersionHigherThanFramework = FALSE;
        }
        return STATUS_SUCCESS;
    }
    /* a client built for a newer framework than this one */
    if (info2 == NULL || minimum > FxMinorVersion || info2->ClientVersionHigherThanFramework == NULL) {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    *info2->ClientVersionHigherThanFramework = TRUE;
    if (BindInfo->FuncCount > WdfFunctionTableNumEntries) {
        BindInfo->FuncCount = WdfFunctionTableNumEntries;
    }
    if (info2->FuncCountPtr != NULL) {
        *info2->FuncCountPtr = BindInfo->FuncCount;
    }
    if (info2->StructCountPtr != NULL) {
        *info2->StructCountPtr = WDF_STRUCTURE_TABLE_NUM_ENTRIES;
    }
    if (info2->StructTable != NULL) {
        *info2->StructTable = g_StructSizes;
    }
    return STATUS_SUCCESS;
}
