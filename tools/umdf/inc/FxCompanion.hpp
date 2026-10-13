/*
 * FxCompanion.hpp — UMDF driver companions (NovaOS stand-in for the
 * unpublished header, README.md)
 *
 * A companion is a UMDF driver loaded beside a KMDF driver for it to call
 * (WdfCompanion*); Microsoft did not publish its implementation and NovaOS
 * does not offer companions: the host never loads a driver as one
 * (WUDF_DRIVER_TYPE_COMPANION), so FxDriver::AddCompanion only sees a
 * WdfCompanionCreate that failed (tools/umdf/rt/companion.cpp).
 */
#pragma once

class FxCompanion : public FxNonPagedObject {
public:
    FxCompanion(PFX_DRIVER_GLOBALS Globals) : FxNonPagedObject(FX_TYPE_COMPANION, sizeof(FxCompanion), Globals) {}
    WDFCOMPANION GetHandle(VOID) { return (WDFCOMPANION)GetObjectHandle(); }
    VOID FinishInitializing(VOID) {}
    VOID DeleteCompanionFromFailedCreate(VOID) { DeleteFromFailedCreate(); }
};
