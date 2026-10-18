/*
 * psnotify.h — what the process manager tells drivers (psnotify.c): the
 * notify routines and object callbacks Windows drivers register, called
 * from um/ where processes, threads and images come and go and where a
 * process or thread handle is opened
 */
#pragma once

#include "../include/types.h"

struct UmProcess;
struct UmObject;

/* A process is about to run its first thread: each process notify routine
 * is told (in the creator's thread).  STATUS_SUCCESS, or the CreationStatus
 * an Ex routine refused it with (the process must not run) */
UINT32 NtPsNotifyProcessCreate(struct UmProcess *p, const char *image_path, const char *cmdline);
/* Its last thread ended (or a refused one goes): the routines are told,
 * then the object drivers were handed for it is let go */
void   NtPsNotifyProcessExit(struct UmProcess *p);
void   NtPsNotifyThread(UINT32 pid, UINT32 tid, bool create);
/* An image mapped into process @pid (0 with @system: a driver), its file
 * @path (C:\...) */
void   NtPsNotifyImage(UINT32 pid, const char *path, UINT64 base, UINT64 size, bool system);
/* A handle to process or thread object @o is being opened (@dup: by
 * NtDuplicateObject) with @access: the Ob pre-operation callbacks may take
 * rights away (the access to grant is returned); the post-operation ones
 * hear the outcome */
UINT32 NtObPreOperation(struct UmObject *o, bool dup, UINT32 access);
void   NtObPostOperation(struct UmObject *o, bool dup, UINT32 granted, UINT32 status);
/* A process or thread open's access with the generic rights and
 * MAXIMUM_ALLOWED made specific, as Windows' generic mapping does */
UINT32 NtPsMapAccess(UINT32 want, bool thread);
/* ldr.c: whether program @f (@name; pinned) is signed well enough to run
 * protected: STATUS_SUCCESS or STATUS_INVALID_IMAGE_HASH */
struct RamNode;
UINT32 NtCiCheckProgram(struct RamNode *f, const char *name);
