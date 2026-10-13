/*
 * fwcfg.h — QEMU's firmware configuration device (fw_cfg): a file the
 * virtual machine's host gives NovaOS with -fw_cfg name=...,string=...
 */
#pragma once
#include "../include/types.h"

/* File @name into @buf, at most @cap bytes: the count, or -1 when this is
 * not QEMU or there is no such file */
int FwCfgFile(const char *name, char *buf, int cap);
