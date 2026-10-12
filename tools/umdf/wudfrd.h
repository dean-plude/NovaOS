/*
 * wudfrd.h — what the reflector (WUDFRd.sys, tools/umdf/wudfrd.c) and the
 * driver host (WUDFHost.exe, tools/umdf/host) say to each other
 *
 * Windows' reflector and host talk over a private ALPC channel whose
 * messages Microsoft never published; NovaOS's are these.  The host opens
 * the device's host channel, the reflector's device interface
 * WUDFRD_HOST_GUID with the reference string "WudfHost", and asks for
 * work with IOCTL_WUDFRD_GET: the reflector completes that request with
 * the next message (a WUDFRD_MSG, then its data), keeping it pending
 * until there is one (the host keeps a GET pending on each of its worker
 * threads, each on a handle of its own, as a synchronous handle carries
 * one request at a time).  The host answers a message with IOCTL_WUDFRD_REPLY
 * (a WUDFRD_REPLY, then the data that goes back), which completes the
 * request the message stood for.  Plain C types only: the reflector is
 * built with the kernel's wdm.h, the host with the Windows headers.
 */
#pragma once

/* {d8a56c37-2a6f-4bd1-8e4b-5f7a1c0e9b20}: a device's host channel */
#define WUDFRD_HOST_GUID_INIT { 0xd8a56c37, 0x2a6f, 0x4bd1, { 0x8e, 0x4b, 0x5f, 0x7a, 0x1c, 0x0e, 0x9b, 0x20 } }
#define WUDFRD_HOST_REF       L"WudfHost"

/* CTL_CODE(FILE_DEVICE_UNKNOWN, fn, METHOD_BUFFERED, FILE_ANY_ACCESS) */
#define WUDFRD_CTL(fn)        ((0x22u << 16) | ((fn) << 2))
/* in: nothing; out: a WUDFRD_HELLO.  Makes this handle the device's host
 * (one at a time) and queues WUDFRD_MSG_START when the device has started */
#define IOCTL_WUDFRD_HELLO    WUDFRD_CTL(0xA00)
/* out: the next WUDFRD_MSG and its data (pending until there is one) */
#define IOCTL_WUDFRD_GET      WUDFRD_CTL(0xA01)
/* in: a WUDFRD_REPLY and its data */
#define IOCTL_WUDFRD_REPLY    WUDFRD_CTL(0xA02)
/* in: a WUDFRD_IFACE; out: the interface's symbolic link (WCHAR[], for
 * WUDFRD_IFACE_REGISTER) */
#define IOCTL_WUDFRD_IFACE    WUDFRD_CTL(0xA03)

/* The largest request data a message carries (a read, write or device
 * control buffer): larger requests fail with STATUS_INVALID_PARAMETER */
#define WUDFRD_MAX_DATA       0x10000u

typedef struct {
    unsigned int version;               /* WUDFRD_VERSION */
    unsigned int started;               /* the device has started (a START message is queued) */
} WUDFRD_HELLO;
#define WUDFRD_VERSION 1

enum {
    WUDFRD_MSG_START = 1,               /* data: the raw CM_RESOURCE_LIST (raw_len bytes), then the translated one */
    WUDFRD_MSG_QUERY_REMOVE,            /* reply: success, or a veto */
    WUDFRD_MSG_CANCEL_REMOVE,
    WUDFRD_MSG_REMOVE,                  /* the host stops the device and exits after its reply */
    WUDFRD_MSG_SURPRISE_REMOVAL,
    WUDFRD_MSG_CREATE,                  /* data: the file name (WCHAR[], what follows the interface's name) */
    WUDFRD_MSG_CLEANUP,
    WUDFRD_MSG_CLOSE,
    WUDFRD_MSG_READ,                    /* reply data: what was read */
    WUDFRD_MSG_WRITE,                   /* data: what is written */
    WUDFRD_MSG_IOCTL,                   /* data: the input buffer; reply data: the output buffer */
};

typedef struct {
    unsigned long long id;              /* echoed in the reply */
    unsigned long long file;            /* the open the request is on (the same for its create, I/O and close) */
    unsigned int       type;            /* WUDFRD_MSG_* */
    unsigned int       pid;             /* the requesting process */
    unsigned int       code;            /* IOCTL: the control code */
    unsigned int       in_len;          /* WRITE, IOCTL: the bytes of data (also data_len) */
    unsigned int       out_len;         /* READ: the length to read; IOCTL: the output buffer's */
    unsigned int       raw_len;         /* START: the raw resource list's bytes */
    long long          offset;          /* READ, WRITE: the byte offset */
    unsigned int       options;         /* CREATE: disposition << 24 | options; also its access and share */
    unsigned int       access, share;
    unsigned int       data_len;        /* the bytes after this header */
} WUDFRD_MSG;

typedef struct {
    unsigned long long id;
    int                status;          /* an NTSTATUS */
    unsigned int       data_len;        /* the bytes after this header */
    unsigned long long information;     /* IoStatus.Information */
} WUDFRD_REPLY;

enum { WUDFRD_IFACE_REGISTER = 1, WUDFRD_IFACE_ENABLE, WUDFRD_IFACE_DISABLE };
typedef struct {
    unsigned int   op;                  /* WUDFRD_IFACE_* */
    unsigned int   reserved;
    unsigned char  guid[16];            /* the interface class */
    unsigned short ref[64];             /* its reference string, or "" */
} WUDFRD_IFACE;
