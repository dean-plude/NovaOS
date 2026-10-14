/*
 * vmbus.h — Hyper-V's VMBus: the synthetic devices of a Hyper-V VM
 *
 * A Generation 2 Hyper-V VM has no PS/2 controller, no USB and no PCI
 * input devices: its keyboard and mouse (and disks, network, video) are
 * channels on VMBus, offered by the host through messages and run over
 * shared-memory rings.  vmbus.c finds Hyper-V, connects, opens the
 * channels a NovaOS driver wants (hv_input.c: the keyboard and the mouse;
 * hv_util.c: the integration services; storvsc.c: the SCSI disks) and
 * polls them.
 */

#pragma once

#include "../include/types.h"

typedef struct VmbusChannel VmbusChannel;

/* A channel driver: the device type it takes (the offer's GUID, as its 16
 * bytes go on the wire), opened() once the channel is open (its context
 * back), packet() for each in-band packet the host sends on it, with the
 * packet's transaction ID (an answer the host waits for carries it back).
 * Optional: completion() for each completion packet (the host's answer to
 * a packet sent with want_completion, with that packet's transaction ID),
 * started() right after opened(), once the context is the channel's (a
 * driver that talks to its device before it is used: storvsc.c) */
typedef struct {
    UINT8       type[16];
    const char *name;
    void      *(*opened)(VmbusChannel *ch);
    void       (*packet)(void *ctx, const UINT8 *data, UINT32 len, UINT64 trans_id);
    void       (*completion)(void *ctx, const UINT8 *data, UINT32 len, UINT64 trans_id);
    void       (*started)(void *ctx);
} VmbusDriver;

/* On CPU 0 with interrupts still off (the SynIC registers are per CPU):
 * find Hyper-V, connect to VMBus and open the channels NovaOS drives.
 * False (one log line) when this is not Hyper-V or the host said no */
bool VmbusInit(void);
/* The device poll thread: read what came on every open channel */
void VmbusPoll(void);

/* A driver sends an in-band packet on its channel; false when its ring is full */
bool VmbusSend(VmbusChannel *ch, const void *data, UINT32 len, UINT64 trans_id, bool want_completion);
/* ... with a buffer the host reads or writes in place (a GPA-direct packet,
 * completion wanted): @buf, @buf_len bytes, physically contiguous
 * (kernel_alloc_pages) */
bool VmbusSendGpa(VmbusChannel *ch, const void *data, UINT32 len, UINT64 trans_id,
                  const void *buf, UINT32 buf_len);
/* Read what came on one channel, calling its driver (a driver waiting for
 * an answer calls this itself, from any thread; VmbusPoll skips a channel
 * someone is reading) */
void VmbusPollChannel(VmbusChannel *ch);

/* The partition reference counter (100 ns since the VM started), which
 * time sync's messages carry; false when the hypervisor offers none */
bool VmbusReferenceTime(UINT64 *t);
