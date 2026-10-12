/*
 * usbdrivr.h — mingw-w64's usbdrivr.h, then NovaOS's usbdlib.h additions
 * (mingw-w64's usbdrivr.h includes its own usbdlib.h by a quoted name,
 * which finds the file beside it rather than tools/kmdf/inc/usbdlib.h)
 */
#pragma once
#include_next <usbdrivr.h>
#include "usbdlib.h"
