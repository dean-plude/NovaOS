/*
 * block.c — the list of block devices (see block.h)
 */

#include "block.h"
#include "../ke/printf.h"

static BlockDev *g_devs[BLOCK_MAX_DEVICES];
static int g_ndevs;
static UINT64 g_read_bytes, g_write_bytes;
static UINT32 g_reads, g_writes;

static bool counted_read(BlockDev *d, UINT64 lba, UINT32 count, void *buf)
{
    bool ok = d->raw_read(d, lba, count, buf);
    if (ok) {
        __atomic_fetch_add(&g_read_bytes, (UINT64)count * BLOCK_SECTOR, __ATOMIC_RELAXED);
        __atomic_fetch_add(&g_reads, 1, __ATOMIC_RELAXED);
    }
    return ok;
}

static bool counted_write(BlockDev *d, UINT64 lba, UINT32 count, const void *buf)
{
    bool ok = d->raw_write(d, lba, count, buf);
    if (ok) {
        __atomic_fetch_add(&g_write_bytes, (UINT64)count * BLOCK_SECTOR, __ATOMIC_RELAXED);
        __atomic_fetch_add(&g_writes, 1, __ATOMIC_RELAXED);
    }
    return ok;
}

void BlockTotals(UINT64 *read_bytes, UINT64 *write_bytes, UINT32 *reads, UINT32 *writes)
{
    *read_bytes = __atomic_load_n(&g_read_bytes, __ATOMIC_RELAXED);
    *write_bytes = __atomic_load_n(&g_write_bytes, __ATOMIC_RELAXED);
    *reads = __atomic_load_n(&g_reads, __ATOMIC_RELAXED);
    *writes = __atomic_load_n(&g_writes, __ATOMIC_RELAXED);
}

void BlockRegister(BlockDev *d)
{
    if (g_ndevs >= BLOCK_MAX_DEVICES) return;
    if (d->read && d->read != counted_read) { d->raw_read = d->read; d->read = counted_read; }
    if (d->write && d->write != counted_write) { d->raw_write = d->write; d->write = counted_write; }
    g_devs[g_ndevs++] = d;
    kprintf("[BLOCK] %s: %llu MiB  %s\n", d->name, (unsigned long long)(d->sectors / 2048), d->model);
}

int BlockCount(void) { return g_ndevs; }
BlockDev *BlockGet(int i) { return i >= 0 && i < g_ndevs ? g_devs[i] : NULL; }

void BlockUnregister(BlockDev *d)
{
    d->gone = true;
    for (int i = 0; i < g_ndevs; i++) {
        if (g_devs[i] != d) continue;
        for (int k = i; k + 1 < g_ndevs; k++) g_devs[k] = g_devs[k + 1];
        g_ndevs--;
        kprintf("[BLOCK] %s removed\n", d->name);
        return;
    }
}
