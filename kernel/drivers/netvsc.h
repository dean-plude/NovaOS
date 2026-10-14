/*
 * netvsc.h — Hyper-V's synthetic network adapter (a VMBus channel)
 */

#pragma once

#include "vmbus.h"
#include "../net/net.h"

/* vmbus.c opens it when the host offers it */
extern const VmbusDriver HvNetworkDriver;

/* The adapter for net.c, once one came up during VmbusInit; NULL otherwise */
const NetAdapter *NetvscAdapter(void);
