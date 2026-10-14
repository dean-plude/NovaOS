/*
 * novacam.h — what NovaOS's USB Video Class driver (kernel/nt/usbvideo.c,
 * the service "usbvideo" as Windows names usbvideo.sys) and Media
 * Foundation's camera source (userland/mf) say to each other
 *
 * Windows' camera stack is an AVStream minidriver under ks.sys, which
 * user mode reaches through Kernel Streaming property sets and pins.
 * NovaOS has no ks.sys yet, so its camera driver answers four control
 * codes of its own on the device interface KSCATEGORY_VIDEO_CAMERA (the
 * one MFEnumDeviceSources lists on Windows 10 and 11):
 *
 *   IOCTL_NOVACAM_GET_INFO    the camera's name, USB IDs and every format
 *                             and frame size it offers (NovaCamInfo);
 *   IOCTL_NOVACAM_START       negotiate one (UVC probe and commit) and
 *                             start the stream for this handle;
 *   IOCTL_NOVACAM_READ_FRAME  the next whole frame (METHOD_OUT_DIRECT: a
 *                             NovaCamFrame, then the frame's bytes);
 *   IOCTL_NOVACAM_STOP        stop the stream.
 *
 * Programs never send these: they use Media Foundation (mf.dll's
 * MFEnumDeviceSources, mfreadwrite.dll's IMFSourceReader).
 */
#pragma once

#define NOVACAM_IOCTL(n, method) ((0x2Fu << 16) | ((0x900u + (n)) << 2) | (method))   /* (FILE_DEVICE_KS) */
#define IOCTL_NOVACAM_GET_INFO    NOVACAM_IOCTL(0, 0)
#define IOCTL_NOVACAM_START       NOVACAM_IOCTL(1, 0)
#define IOCTL_NOVACAM_READ_FRAME  NOVACAM_IOCTL(2, 2)
#define IOCTL_NOVACAM_STOP        NOVACAM_IOCTL(3, 0)

#define NOVACAM_MAX_MODES 48

/* FOURCCs of the formats a camera may send */
#define NOVACAM_FOURCC(a, b, c, d) ((unsigned int)(a) | (unsigned int)(b) << 8 | (unsigned int)(c) << 16 | (unsigned int)(d) << 24)
#define NOVACAM_YUY2 NOVACAM_FOURCC('Y', 'U', 'Y', '2')
#define NOVACAM_NV12 NOVACAM_FOURCC('N', 'V', '1', '2')
#define NOVACAM_MJPG NOVACAM_FOURCC('M', 'J', 'P', 'G')

typedef struct {
    unsigned int   fourcc;             /* NOVACAM_YUY2, ... (another: the GUID's first four bytes) */
    unsigned short width, height;
    unsigned int   interval;           /* the default frame interval, 100 ns units */
    unsigned int   min_interval;       /* the shortest it offers */
    unsigned int   max_bytes;          /* a frame's largest size */
    unsigned char  format, frame;      /* bFormatIndex, bFrameIndex */
    unsigned char  bpp, reserved;
} NovaCamMode;

typedef struct {
    unsigned short vid, pid;
    unsigned int   nmodes;
    unsigned short name[64];           /* the product string (UTF-16, NUL-ended) */
    NovaCamMode    modes[NOVACAM_MAX_MODES];
} NovaCamInfo;

typedef struct {                       /* IOCTL_NOVACAM_START's input */
    unsigned int mode;                 /* an index into NovaCamInfo.modes */
    unsigned int interval;             /* 100 ns units; 0: the mode's default */
} NovaCamStart;

typedef struct {                       /* IOCTL_NOVACAM_READ_FRAME's output, before the frame */
    unsigned int       bytes;          /* the frame's size */
    unsigned int       seq;            /* frames delivered since START (1 first) */
    unsigned long long reserved;
} NovaCamFrame;
