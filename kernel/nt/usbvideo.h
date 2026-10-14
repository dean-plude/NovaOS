/*
 * usbvideo.h — NovaOS's USB Video Class driver (usbvideo.c), built into
 * the kernel as the service "usbvideo"
 */
#pragma once
#include "wdm.h"

NTSTATUS NT_API UsbVideoDriverEntry(NT_DRIVER_OBJECT *drv, NT_UNICODE_STRING *reg);
