/*
 * hv_util.h — Hyper-V's integration services: shutdown, time sync, heartbeat,
 * key-value exchange
 */

#pragma once

#include "vmbus.h"

/* vmbus.c opens these channels when the host offers them */
extern const VmbusDriver HvShutdownDriver;
extern const VmbusDriver HvTimeSyncDriver;
extern const VmbusDriver HvHeartbeatDriver;
extern const VmbusDriver HvKvpDriver;
