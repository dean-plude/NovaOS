/*
 * sbc.c — an SBC encoder, the codec every A2DP headset must decode
 *
 * Written from the A2DP specification's description of the codec
 * (Advanced Audio Distribution Profile 1.3, section 12 and its Appendix B):
 * an 8-band polyphase analysis filter bank over 80 samples, a scale factor
 * per band, bits shared out among the bands by the loudness or SNR rule
 * from a bitpool, each band's samples quantized to its bits, and, in joint
 * stereo, the bands where mid and side cost fewer bits sent that way.  The
 * kernel has no floating point, so the filter runs in fixed point: the
 * window's coefficients (the specification's table of 80, the prototype
 * filter with every other group of 16 negated) in Q31, the cosine matrix
 * in Q15, and the band samples kept with 12 fractional bits.  (Linux's and
 * Android's SBC code is LGPL and Apache, so this is new.)
 */
#include "sbc.h"
#include "../lib/string.h"

#define FRAC 12                        /* fractional bits of a band sample */

/* The analysis window, C[0..79], in Q31 */
static const INT32 g_c[80] = {
    0, 336243, 737138, 1191038, 1769354, 2447970, 3170548, 3830504,
    4320362, 4517704, 4283254, 3471542, 1937362, -383982, -3542770, -7510125,
    12153672, 17243030, 22459338, 27374475, 31466061, 34154783, 34834004, 32896036,
    27782384, 19021498, 6279423, -10556558, -31440036, -56070530, -83913220, -114218864,
    146026618, 178208410, 209541558, 238793071, 264708601, 286183152, 302265850, 312222319,
    315583606, 312222319, 302265850, 286183152, 264708601, 238793071, 209541558, 178208410,
    -146026618, -114218864, -83913220, -56070530, -31440036, -10556558, 6279423, 19021498,
    27782384, 32896036, 34834004, 34154783, 31466061, 27374475, 22459338, 17243030,
    -12153672, -7510125, -3542770, -383982, 1937362, 3471542, 4283254, 4517704,
    4320362, 3830504, 3170548, 2447970, 1769354, 1191038, 737138, 336243,
};

/* M[k][i] = cos((k + 0.5) (i - 4) pi / 8), in Q15 */
static const INT16 g_m[8][16] = {
    { 23170, 27246, 30274, 32138, 32767, 32138, 30274, 27246, 23170, 18205, 12540, 6393, 0, -6393, -12540, -18205 },
    { -23170, -6393, 12540, 27246, 32767, 27246, 12540, -6393, -23170, -32138, -30274, -18205, 0, 18205, 30274, 32138 },
    { -23170, -32138, -12540, 18205, 32767, 18205, -12540, -32138, -23170, 6393, 30274, 27246, 0, -27246, -30274, -6393 },
    { 23170, -18205, -30274, 6393, 32767, 6393, -30274, -18205, 23170, 27246, -12540, -32138, 0, 32138, 12540, -27246 },
    { 23170, 18205, -30274, -6393, 32767, -6393, -30274, 18205, 23170, -27246, -12540, 32138, 0, -32138, 12540, 27246 },
    { -23170, 32138, -12540, -18205, 32767, -18205, -12540, 32138, -23170, -6393, 30274, -27246, 0, 27246, -30274, 6393 },
    { -23170, 6393, 12540, -27246, 32767, -27246, 12540, 6393, -23170, 32138, -30274, 18205, 0, -18205, 30274, -32138 },
    { 23170, -27246, 30274, -32138, 32767, -32138, 30274, -27246, 23170, -18205, 12540, -6393, 0, 6393, -12540, 18205 },
};

/* The loudness rule's offsets for 8 bands, by sampling rate (16, 32, 44.1, 48 kHz) */
static const INT8 g_offset8[4][8] = {
    { -2, 0, 0, 0, 0, 0, 0, 1 },
    { -3, 0, 0, 0, 0, 0, 1, 2 },
    { -4, 0, 0, 0, 0, 0, 1, 2 },
    { -4, 0, 0, 0, 0, 0, 1, 2 },
};

static int rate_index(UINT32 rate)
{
    return rate == 16000 ? 0 : rate == 32000 ? 1 : rate == 44100 ? 2 : rate == 48000 ? 3 : -1;
}

static int channels(const SbcEncoder *e) { return e->mode == SBC_MONO ? 1 : 2; }

bool SbcInit(SbcEncoder *e)
{
    memset(e->x, 0, sizeof(e->x));
    UINT32 max = (e->mode == SBC_MONO || e->mode == SBC_DUAL ? 16u : 32u) * e->subbands;
    return rate_index(e->rate) >= 0 && e->subbands == 8 && (e->blocks == 4 || e->blocks == 8 || e->blocks == 12 ||
           e->blocks == 16) && e->mode <= SBC_JOINT && e->bitpool >= 2 && e->bitpool <= (max < 250 ? max : 250);
}

UINT32 SbcFrameSamples(const SbcEncoder *e) { return (UINT32)e->blocks * e->subbands; }

UINT32 SbcFrameLength(const SbcEncoder *e)
{
    UINT32 nch = (UINT32)channels(e), bits;
    if (e->mode == SBC_MONO || e->mode == SBC_DUAL) bits = e->blocks * nch * e->bitpool;
    else bits = (e->mode == SBC_JOINT ? e->subbands : 0) + (UINT32)e->blocks * e->bitpool;
    return 4 + (4 * e->subbands * nch) / 8 + (bits + 7) / 8;
}

/* Eight new samples of channel @ch (@pcm, @step apart) through the
 * analysis filter: eight band samples (FRAC fractional bits) into @sb */
static void analyse(SbcEncoder *e, int ch, const INT16 *pcm, int step, bool mono, INT32 *sb)
{
    INT32 *x = e->x[ch];
    memmove(x + 8, x, 72 * sizeof(INT32));
    for (int i = 0; i < 8; i++)                                 /* (X[7] is the first of them, X[0] the last) */
        x[7 - i] = mono ? ((INT32)pcm[i * step] + pcm[i * step + 1]) / 2 : pcm[i * step];
    INT64 y[16];
    for (int i = 0; i < 16; i++) {
        INT64 s = 0;
        for (int j = 0; j < 5; j++) s += (INT64)g_c[i + 16 * j] * x[i + 16 * j];
        y[i] = s >> 16;                                         /* (Q15) */
    }
    for (int k = 0; k < 8; k++) {
        INT64 s = 0;
        for (int i = 0; i < 16; i++) s += g_m[k][i] * y[i];
        sb[k] = (INT32)(s >> (30 - FRAC));
    }
}

/* The smallest scale factor (0..15) whose range holds every sample of a
 * band: |s| < 2^(sf + 1) */
static UINT8 scale_factor(const INT32 *s, int n, int stride)
{
    INT32 max = 0;
    for (int i = 0; i < n; i++) {
        INT32 v = s[i * stride] < 0 ? -s[i * stride] : s[i * stride];
        if (v > max) max = v;
    }
    UINT8 sf = 0;
    while (sf < 15 && max >= (1 << (sf + 1 + FRAC))) sf++;
    return sf;
}

/* The bits each band gets (A2DP 12.6.4): @bits[ch][sb] */
static void allocate(const SbcEncoder *e, UINT8 sf[2][8], int bits[2][8])
{
    int nch = channels(e), nsb = e->subbands, ri = rate_index(e->rate);
    int need[2][8];
    for (int ch = 0; ch < nch; ch++)
        for (int sb = 0; sb < nsb; sb++) {
            if (e->snr) need[ch][sb] = sf[ch][sb];
            else if (!sf[ch][sb]) need[ch][sb] = -5;
            else {
                int loud = sf[ch][sb] - g_offset8[ri][sb];
                need[ch][sb] = loud > 0 ? loud / 2 : loud;
            }
        }
    /* mono and dual channel: each channel from the bitpool on its own;
     * stereo and joint stereo: both together */
    int groups = e->mode == SBC_MONO || e->mode == SBC_DUAL ? nch : 1;
    for (int g = 0; g < groups; g++) {
        int c0 = groups == 1 ? 0 : g, c1 = groups == 1 ? nch - 1 : g;
        int maxneed = 0;
        for (int ch = c0; ch <= c1; ch++)
            for (int sb = 0; sb < nsb; sb++) if (need[ch][sb] > maxneed) maxneed = need[ch][sb];
        int count = 0, slices = 0, slice = maxneed + 1;
        do {
            slice--;
            count += slices;
            slices = 0;
            for (int ch = c0; ch <= c1; ch++)
                for (int sb = 0; sb < nsb; sb++) {
                    if (need[ch][sb] > slice + 1 && need[ch][sb] < slice + 16) slices++;
                    else if (need[ch][sb] == slice + 1) slices += 2;
                }
        } while (count + slices < e->bitpool);
        if (count + slices == e->bitpool) { count += slices; slice--; }
        for (int ch = c0; ch <= c1; ch++)
            for (int sb = 0; sb < nsb; sb++) {
                if (need[ch][sb] < slice + 2) bits[ch][sb] = 0;
                else bits[ch][sb] = need[ch][sb] - slice < 16 ? need[ch][sb] - slice : 16;
            }
        /* what is left: first to bands that have bits (or are next in line), then any */
        int ch = c0, sb = 0;
        while (count < e->bitpool && sb < nsb) {
            if (bits[ch][sb] >= 2 && bits[ch][sb] < 16) { bits[ch][sb]++; count++; }
            else if (need[ch][sb] == slice + 1 && e->bitpool > count + 1) { bits[ch][sb] = 2; count += 2; }
            if (ch == c1) { ch = c0; sb++; } else ch++;
        }
        ch = c0;
        sb = 0;
        while (count < e->bitpool && sb < nsb) {
            if (bits[ch][sb] < 16) { bits[ch][sb]++; count++; }
            if (ch == c1) { ch = c0; sb++; } else ch++;
        }
    }
}

typedef struct { UINT8 *p; UINT32 bit; } Bits;

static void put(Bits *b, UINT32 v, int n)
{
    while (n-- > 0) {
        UINT32 byte = b->bit >> 3;
        if (!(b->bit & 7)) b->p[byte] = 0;
        if ((v >> n) & 1) b->p[byte] |= (UINT8)(0x80 >> (b->bit & 7));
        b->bit++;
    }
}

/* CRC-8, x^8 + x^4 + x^3 + x^2 + 1, from 0x0F, over @n bits of @p */
static UINT8 crc8(const UINT8 *p, UINT32 n)
{
    UINT8 crc = 0x0F;
    for (UINT32 i = 0; i < n; i++) {
        UINT8 bit = (UINT8)((p[i >> 3] >> (7 - (i & 7))) & 1);
        UINT8 top = (UINT8)(crc >> 7);
        crc = (UINT8)(crc << 1);
        if (top ^ bit) crc ^= 0x1D;
    }
    return crc;
}

UINT32 SbcEncode(SbcEncoder *e, const INT16 *pcm, UINT8 *out)
{
    int nch = channels(e), nsb = e->subbands, nblk = e->blocks;
    INT32 s[16][2][8];
    for (int blk = 0; blk < nblk; blk++)
        for (int ch = 0; ch < nch; ch++)
            analyse(e, ch, pcm + blk * nsb * 2 + ch, 2, nch == 1, s[blk][ch]);
    UINT8 sf[2][8];
    for (int ch = 0; ch < nch; ch++)
        for (int sb = 0; sb < nsb; sb++) sf[ch][sb] = scale_factor(&s[0][ch][sb], nblk, 16);
    UINT8 join = 0;
    if (e->mode == SBC_JOINT) {
        /* mid and side (L + R)/2, (L - R)/2 where their scale factors sum
         * lower (every band but the last) */
        for (int sb = 0; sb < nsb - 1; sb++) {
            INT32 m[16], d[16];
            for (int blk = 0; blk < nblk; blk++) {
                m[blk] = (s[blk][0][sb] >> 1) + (s[blk][1][sb] >> 1);
                d[blk] = (s[blk][0][sb] >> 1) - (s[blk][1][sb] >> 1);
            }
            UINT8 sm = scale_factor(m, nblk, 1), sd = scale_factor(d, nblk, 1);
            if (sm + sd < sf[0][sb] + sf[1][sb]) {
                join |= (UINT8)(0x80 >> sb);
                sf[0][sb] = sm;
                sf[1][sb] = sd;
                for (int blk = 0; blk < nblk; blk++) { s[blk][0][sb] = m[blk]; s[blk][1][sb] = d[blk]; }
            }
        }
    }
    int bits[2][8];
    allocate(e, sf, bits);

    Bits b = { out, 0 };
    put(&b, 0x9C, 8);                                           /* syncword */
    put(&b, (UINT32)rate_index(e->rate), 2);
    put(&b, (UINT32)(nblk / 4 - 1), 2);
    put(&b, e->mode, 2);
    put(&b, e->snr ? 1 : 0, 1);
    put(&b, nsb == 8 ? 1 : 0, 1);
    put(&b, e->bitpool, 8);
    put(&b, 0, 8);                                              /* (the CRC, below) */
    if (e->mode == SBC_JOINT) put(&b, join, nsb);
    for (int ch = 0; ch < nch; ch++)
        for (int sb = 0; sb < nsb; sb++) put(&b, sf[ch][sb], 4);
    /* the CRC covers the header's second and third bytes, the join bits
     * and the scale factors */
    UINT8 tmp[2 + 1 + 8 + 1];
    UINT32 nbits = b.bit - 32;
    tmp[0] = out[1];
    tmp[1] = out[2];
    for (UINT32 i = 0; i < (nbits + 7) / 8; i++) tmp[2 + i] = out[4 + i];
    out[3] = crc8(tmp, 16 + nbits);
    for (int blk = 0; blk < nblk; blk++)
        for (int ch = 0; ch < nch; ch++)
            for (int sb = 0; sb < nsb; sb++) {
                int n = bits[ch][sb];
                if (!n) continue;
                INT64 levels = ((INT64)1 << n) - 1;
                int shift = sf[ch][sb] + 1 + FRAC;
                INT64 q = (((INT64)s[blk][ch][sb] + ((INT64)1 << shift)) * levels) >> (shift + 1);
                if (q < 0) q = 0;
                if (q > levels) q = levels;
                put(&b, (UINT32)q, n);
            }
    UINT32 len = (b.bit + 7) / 8;
    while (len < SbcFrameLength(e)) out[len++] = 0;
    return len;
}
