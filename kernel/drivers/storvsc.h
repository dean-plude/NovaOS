/*
 * storvsc.h — Hyper-V's synthetic SCSI controller (a VMBus channel)
 */

#pragma once

#include "vmbus.h"

/* vmbus.c opens a channel per SCSI controller the host offers; its disks
 * become block devices hv0, hv1, ... */
extern const VmbusDriver HvStorageDriver;
