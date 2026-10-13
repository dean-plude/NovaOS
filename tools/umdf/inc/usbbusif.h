/*
 * usbbusif.h (NovaOS UMDF stand-in) - mingw-w64's ddk/usbbusif.h, but without
 * its own definition of USB_BUS_INTERFACE_USBDI_GUID: Microsoft's
 * usbstubum.cpp defines that GUID itself under INITGUID before it includes
 * this header, so mingw's copy (renamed here) would define it twice.
 */
#pragma once
#define USB_BUS_INTERFACE_USBDI_GUID NovaMingw_USB_BUS_INTERFACE_USBDI_GUID
#include <ddk/usbbusif.h>
#undef USB_BUS_INTERFACE_USBDI_GUID
EXTERN_C const GUID USB_BUS_INTERFACE_USBDI_GUID;
