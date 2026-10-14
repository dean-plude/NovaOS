/*
 * hv_video.h — Hyper-V's synthetic video device (a VMBus channel)
 */

#pragma once

#include "vmbus.h"

/* vmbus.c opens it when the host offers it; it takes over the boot display
 * (display.c's DisplayHyperVAttach) */
extern const VmbusDriver HvVideoDriver;
