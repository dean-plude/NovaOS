/*
 * sbc.h — an SBC encoder (the A2DP specification's mandatory codec), for
 * Bluetooth stereo audio (bthaudio.c)
 */
#pragma once
#include "../include/types.h"

#define SBC_MONO     0
#define SBC_DUAL     1
#define SBC_STEREO   2
#define SBC_JOINT    3

typedef struct {
    /* the stream's parameters (A2DP's Codec Specific Information Elements) */
    UINT32 rate;                       /* 16000, 32000, 44100 or 48000 */
    UINT8  mode;                       /* SBC_MONO ... SBC_JOINT */
    UINT8  blocks;                     /* 4, 8, 12 or 16 */
    UINT8  subbands;                   /* 8 (the only count this encoder makes) */
    UINT8  snr;                        /* bit allocation: 1 SNR, 0 loudness */
    UINT8  bitpool;
    /* the analysis filter's history, a channel each */
    INT32  x[2][80];
} SbcEncoder;

/* Set up @e for its parameters (filled in by the caller); false if this
 * encoder cannot make them */
bool   SbcInit(SbcEncoder *e);
/* Bytes one frame takes */
UINT32 SbcFrameLength(const SbcEncoder *e);
/* Samples (a channel) one frame holds: blocks x subbands */
UINT32 SbcFrameSamples(const SbcEncoder *e);
/* Encode one frame from @pcm (interleaved s16, two channels: a mono
 * stream takes their mean) into @out (SbcFrameLength bytes); its length */
UINT32 SbcEncode(SbcEncoder *e, const INT16 *pcm, UINT8 *out);
