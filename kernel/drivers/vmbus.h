/*
 * vmbus.h — Hyper-V's VMBus: the synthetic devices of a Hyper-V VM
 *
 * A Generation 2 Hyper-V VM has no PS/2 controller, no USB and no PCI
 * input devices: its keyboard and mouse (and disks, network, video) are
 * channels on VMBus, offered by the host through messages and run over
 * shared-memory rings.  vmbus.c finds Hyper-V, connects, opens the
 * channels a NovaOS driver wants (hv_input.c: the keyboard and the mouse;
 * hv_util.c: the integration services; storvsc.c: the SCSI disks;
 * hv_video.c: the video; netvsc.c: the network adapter) and polls them.
 */

#pragma once

#include "../include/types.h"

typedef struct VmbusChannel VmbusChannel;

/* A piece of a buffer the guest gave the host (VmbusCreateGpadl) that a
 * transfer-page packet points at */
typedef struct {
    UINT32 len, offset;
} VmbusXferRange;

/* A channel driver: the device type it takes (the offer's GUID, as its 16
 * bytes go on the wire), opened() once the channel is open (its context
 * back), packet() for each in-band packet the host sends on it, with the
 * packet's transaction ID (an answer the host waits for carries it back).
 * Optional: completion() for each completion packet (the host's answer to
 * a packet sent with want_completion, with that packet's transaction ID),
 * started() right after opened(), once the context is the channel's (a
 * driver that talks to its device before it is used: storvsc.c), tick()
 * on every VmbusPoll (the "vmbus" thread: each time the host signals,
 * and every 10 ms timer tick), xfer() for each transfer-page packet (the
 * ranges of the driver's buffer it points at, then its in-band part; the
 * host waits for a completion packet with its transaction ID:
 * VmbusSendCompletion), removed() when the host takes the device away
 * (removed from the VM's settings while it runs: the channel is closed
 * after it returns; its sends go nowhere from then on).  A device the
 * host offers while NovaOS runs is opened as at start.  The callbacks of
 * one channel never run at the same time */
typedef struct {
    UINT8       type[16];
    const char *name;
    void      *(*opened)(VmbusChannel *ch);
    void       (*packet)(void *ctx, const UINT8 *data, UINT32 len, UINT64 trans_id);
    void       (*completion)(void *ctx, const UINT8 *data, UINT32 len, UINT64 trans_id);
    void       (*started)(void *ctx);
    void       (*tick)(void *ctx);
    void       (*xfer)(void *ctx, const VmbusXferRange *r, UINT32 n, const UINT8 *data, UINT32 len,
                       UINT64 trans_id);
    void       (*removed)(void *ctx);
} VmbusDriver;

/* On CPU 0 with interrupts still off (the SynIC registers are per CPU):
 * find Hyper-V, connect to VMBus and open the channels NovaOS drives.
 * False (one log line) when this is not Hyper-V or the host said no */
bool VmbusInit(void);
/* Start the "vmbus" thread: woken by the host's interrupt (SINT2: a
 * message, or a channel's bit in the event flags), it reads every open
 * channel and the host's messages (devices added and removed) */
void VmbusStart(void);
/* Read what came on every open channel (the "vmbus" thread) */
void VmbusPoll(void);

/* A driver sends an in-band packet on its channel; false when its ring is full */
bool VmbusSend(VmbusChannel *ch, const void *data, UINT32 len, UINT64 trans_id, bool want_completion);
/* ... with a buffer the host reads or writes in place (a GPA-direct packet,
 * completion wanted): @buf, @buf_len bytes, physically contiguous
 * (kernel_alloc_pages) */
bool VmbusSendGpa(VmbusChannel *ch, const void *data, UINT32 len, UINT64 trans_id,
                  const void *buf, UINT32 buf_len);
/* A completion packet (the answer to a packet the host sent with its
 * completion wanted, carrying its transaction ID) */
bool VmbusSendCompletion(VmbusChannel *ch, const void *data, UINT32 len, UINT64 trans_id);
/* Give the host @buf (@len bytes, a multiple of 4096, physically
 * contiguous: kernel_alloc_pages) as a buffer it reads and writes; *gpadl
 * names it in the device's own messages.  Only from started() (during
 * VmbusInit, or on the "vmbus" thread for a device added later: it waits
 * for the host's answer on the message slot); at most 4 per channel */
bool VmbusCreateGpadl(VmbusChannel *ch, void *buf, UINT32 len, UINT32 *gpadl);
/* Read what came on one channel, calling its driver (a driver waiting for
 * an answer calls this itself, from any thread; VmbusPoll skips a channel
 * someone is reading) */
void VmbusPollChannel(VmbusChannel *ch);
/* The MMIO space the host's offer gives the device, in MiB (synthetic
 * video: the size of its video memory) */
UINT32 VmbusChannelMmioMegabytes(const VmbusChannel *ch);

/* The partition reference counter (100 ns since the VM started), which
 * time sync's messages carry; false when the hypervisor offers none */
bool VmbusReferenceTime(UINT64 *t);
