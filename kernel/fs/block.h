/*
 * block.h — block devices (disks), as registered by the storage drivers
 */

#pragma once

#include "../include/types.h"

#define BLOCK_MAX_DEVICES 8

typedef struct BlockDev BlockDev;
struct BlockDev {
    char    name[16];           /* e.g. "sata0" */
    char    model[41];          /* from IDENTIFY */
    UINT64  sectors;            /* 512-byte sectors */
    /* Transfer @count sectors at @lba; @buf needs no particular alignment. */
    bool  (*read)(BlockDev *d, UINT64 lba, UINT32 count, void *buf);
    bool  (*write)(BlockDev *d, UINT64 lba, UINT32 count, const void *buf);
    bool  (*flush)(BlockDev *d);
    void   *ctx;
    bool    removable;          /* USB: may go away at any time */
    volatile bool gone;         /* it went away: every transfer fails */
    /* (BlockRegister: the driver's own transfer functions, which read and
     * write call through, counting what moves for BlockTotals) */
    bool  (*raw_read)(BlockDev *d, UINT64 lba, UINT32 count, void *buf);
    bool  (*raw_write)(BlockDev *d, UINT64 lba, UINT32 count, const void *buf);
};

#define BLOCK_SECTOR 512

void      BlockRegister(BlockDev *d);
/* The device went away: it leaves the list (the struct stays valid) */
void      BlockUnregister(BlockDev *d);
int       BlockCount(void);
BlockDev *BlockGet(int i);
/* Bytes and transfers read and written on every disk since boot (Task
 * Manager's disk graph, SystemPerformanceInformation's I/O counts) */
void      BlockTotals(UINT64 *read_bytes, UINT64 *write_bytes, UINT32 *reads, UINT32 *writes);
