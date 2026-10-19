/*
 * bthaudio.c — a Bluetooth headset's audio: hands-free voice (HFP, with
 * its microphone) and stereo (A2DP), as Windows' BthHFAud and BthA2dp
 * drivers give a headset's
 *
 * When a paired headset's Hands-Free (0x111E) or Audio Sink (0x110B)
 * service is switched on (bthprops.cpl's BluetoothSetServiceState, which
 * reaches bthport.c), this connects them and, while they are connected,
 * NovaOS's mixer has the headset as Windows lists it:
 *
 *   - "Headphones (NAME)": A2DP's stereo, the default output once it is
 *     there.  AVDTP (Bluetooth's Audio/Video Distribution Transport
 *     Protocol 1.3) on L2CAP PSM 0x19: discover the headset's stream end
 *     points, read the sink's SBC capabilities, configure it (48 kHz, else
 *     44.1 kHz; joint stereo; 16 blocks, 8 subbands; loudness; bitpool up
 *     to 53, as Windows uses), open its media channel, then start the
 *     stream while something plays and suspend it after two seconds of
 *     silence.  The output's ring is taken at the stream's rate, encoded
 *     by sbc.c and sent in RTP packets of as many frames as the channel's
 *     MTU holds;
 *   - "Headset (NAME Hands-Free)", an output and a microphone: HFP 1.8's
 *     audio gateway (the computer's side).  RFCOMM (TS 07.10 as Bluetooth's
 *     RFCOMM 1.2 uses it: multiplexer start-up, parameter negotiation with
 *     credit-based flow control, modem status) on L2CAP PSM 3 to the
 *     headset's Hands-Free channel (from its SDP record), on which the
 *     headset sets up the service level connection with AT commands (its
 *     features, the indicators, event reporting) that this answers as a
 *     gateway with no telephone does.  Voice goes on a synchronous link
 *     (bthport.c's BthScoOpen: eSCO or SCO, CVSD, 8 kHz 16-bit samples),
 *     opened while the output plays or the microphone records and closed
 *     after two seconds of neither.
 *
 * A thread a headset runs everything: connecting, then every 10 ms taking
 * what the outputs' rings hold (their positions move at their rates, so
 * the mixer stays ahead of them as of a sound card) and sending it, while
 * the microphone's samples arrive from the voice link on the transport's
 * thread.  When the headset goes (its link drops, a channel closes) its
 * devices leave the mixer and it is tried again every 10 seconds.
 *
 * Not done: wideband speech (mSBC), AAC and other codecs, absolute volume
 * (AVRCP), remote control buttons, the headset connecting to us.
 */
#include "ntapi.h"
#include "bthport.h"
#include "sbc.h"
#include "../arch/x86_64/cpu.h"
#include "../drivers/audio.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../lib/string.h"
#include "../mm/vmm.h"

#define TICK_MS      10
#define RETRY_MS     10000
#define IDLE_MS      2000              /* silence before the stream or voice link is let go */
#define A2DP_FRAMES  8192              /* the stereo output's ring: frames */
#define HF_RATE      8000
#define HF_FRAMES    2048              /* the hands-free output's ring (stereo) and the microphone's (mono) */
#define SCO_CHUNK    24                /* samples sent on the voice link at a time (48 bytes) */
#define MAX_PKT      1024              /* a media packet */
#define PSM_RFCOMM   0x0003
#define PSM_AVDTP    0x0019

/* RFCOMM frame types (the control field without its P/F bit) */
#define RF_SABM 0x2F
#define RF_UA   0x63
#define RF_DM   0x0F
#define RF_DISC 0x43
#define RF_UIH  0xEF
#define RF_PF   0x10
/* multiplexer commands' type octets (type << 2 | C/R << 1 | EA) */
#define MUX_PN  0x80
#define MUX_MSC 0xE0
#define MUX_NSC 0x10
#define CREDITS 7                      /* given to the headset at a time */

/* AVDTP signals */
#define AV_DISCOVER   0x01
#define AV_GET_CAPS   0x02
#define AV_SET_CONFIG 0x03
#define AV_OPEN       0x06
#define AV_START      0x07
#define AV_CLOSE      0x08
#define AV_SUSPEND    0x09
#define AV_ABORT      0x0A
#define AV_GET_ALL    0x0C
#define AV_DELAY      0x0D

struct BthAudio {
    BthRadio *r;
    BthDev   *d;
    char      name[64];
    bool      hfp, a2dp;
    UINT8     chan;
    volatile bool stop;
    NT_KEVENT done;
    /* RFCOMM and the hands-free service level connection */
    BthChan  *rfc;
    UINT8     dlci;
    volatile int mux_ua, dlc_ua, pn;   /* 0 waiting, 1 yes, -1 refused */
    volatile bool msc;
    bool      cfc;                     /* credit-based flow control */
    volatile INT32 tx_credits;
    volatile INT32 rx_credits;         /* given and not used yet */
    UINT32    frame;                   /* the DLC's frame size */
    char      at[256];                 /* AT commands from the headset, not yet answered */
    volatile UINT32 at_len;
    NT_KEVENT at_lock;
    volatile bool slc, hfp_up;
    /* A2DP */
    BthChan  *sig, *media;
    UINT8     label, acp;
    volatile UINT32 want;              /* (label << 8 | signal) of the command waiting for its answer */
    volatile int got;                  /* its message type (2 accept, 3 reject), 0 none yet */
    UINT8     resp[256];
    volatile UINT32 resp_len;
    SbcEncoder sbc;
    volatile bool streaming, a2dp_up;
    UINT16    seq;
    UINT32    ts;
    /* the mixer's devices */
    AudioOutput aout, hout;
    AudioInput  hin;
    bool      aout_on, hout_on, hin_on;
    char      aname[96], hname[96], mname[96], akey[48], hkey[48];
    INT16    *aring, *hring, *iring;
    volatile UINT32 apos, hpos, ipos;  /* bytes */
    volatile bool rec;                 /* the microphone records */
    UINT64    t0, adone, hdone;        /* when the devices came, frames taken from each ring since */
    UINT64    a_heard, h_heard;        /* when something last played */
    INT16     pend[2 * 128];           /* stereo frames for the next SBC frame */
    UINT32    npend;
    UINT8     pkt[MAX_PKT];            /* the media packet being filled */
    UINT32    pkt_len, pkt_frames;
    INT16     voice[SCO_CHUNK];
    UINT32    nvoice;
};

static UINT64 now_ms(void) { return sched_tsc_100ns(rdtsc()) / 10000; }

static void sleep_ms(UINT32 ms)
{
    INT64 t = -(INT64)ms * 10000;
    nt_KeDelayExecutionThread(0, 0, &t);
}

/* Wait (10 ms steps) until @cond, at most @ms, while the headset is wanted */
#define WAIT(a, cond, ms) ({ int _n = (int)((ms) / 10); while (!(cond) && _n-- > 0 && !(a)->stop) sleep_ms(10); (bool)(cond); })

static UINT16 be16(const UINT8 *p) { return (UINT16)(p[0] << 8 | p[1]); }

/* ---- RFCOMM ---- */

static UINT8 g_crc[256];

static void crc_init(void)
{
    for (int i = 0; i < 256; i++) {
        UINT8 c = (UINT8)i;
        for (int k = 0; k < 8; k++) c = c & 1 ? (UINT8)((c >> 1) ^ 0xE0) : (UINT8)(c >> 1);
        g_crc[i] = c;
    }
}

static UINT8 fcs(const UINT8 *p, int n)
{
    UINT8 f = 0xFF;
    for (int i = 0; i < n; i++) f = g_crc[f ^ p[i]];
    return (UINT8)(0xFF - f);
}

/* A frame on DLCI @dlci: @ctl (P/F included), @info; @credits (>= 0): a
 * credit octet first (UIH with P set).  We started the session, so our
 * commands and data carry C/R 1 and our responses 0. */
static NT_STATUS rf_send(BthAudio *a, UINT8 dlci, UINT8 ctl, bool cmd, int credits, const UINT8 *info, UINT32 n)
{
    UINT8 f[8 + 256];
    if (n > 250 || !a->rfc) return NT_STATUS_INVALID_PARAMETER;
    UINT32 k = 0;
    f[k++] = (UINT8)(dlci << 2 | (cmd ? 2 : 0) | 1);
    f[k++] = credits >= 0 ? (UINT8)(ctl | RF_PF) : ctl;
    if (n < 128) f[k++] = (UINT8)(n << 1 | 1);
    else { f[k++] = (UINT8)(n << 1); f[k++] = (UINT8)(n >> 7); }
    if (credits >= 0) f[k++] = (UINT8)credits;
    memcpy(f + k, info, n);
    k += n;
    f[k] = fcs(f, (ctl & ~RF_PF) == RF_UIH ? 2 : 3);
    return BthChanSend(a->r, a->rfc, f, k + 1);
}

static NT_STATUS rf_mux(BthAudio *a, UINT8 type, bool cmd, const UINT8 *v, UINT8 n)
{
    UINT8 b[16];
    b[0] = (UINT8)(type | (cmd ? 2 : 0) | 1);
    b[1] = (UINT8)(n << 1 | 1);
    memcpy(b + 2, v, n);
    return rf_send(a, 0, RF_UIH, true, -1, b, 2u + n);
}

/* A multiplexer command or response from the headset (on DLCI 0) */
static void rf_mux_in(BthAudio *a, const UINT8 *p, UINT32 n)
{
    if (n < 2) return;
    UINT8 type = p[0] & 0xFC;
    bool cmd = p[0] & 2;
    UINT32 len = p[1] >> 1;
    const UINT8 *v = p + 2;
    if (2 + len > n) return;
    if (type == MUX_PN && len >= 8) {
        if (cmd) {                                              /* (its own: taken as it asks) */
            UINT8 r[8];
            memcpy(r, v, 8);
            if ((r[1] & 0xF0) == 0xF0) r[1] = (UINT8)((r[1] & 0x0F) | 0xE0);
            rf_mux(a, MUX_PN, false, r, 8);
        } else if ((v[0] & 0x3F) == a->dlci) {
            a->cfc = (v[1] & 0xF0) == 0xE0;
            a->tx_credits = a->cfc ? (v[7] & 7) : 0;
            UINT32 fs = (UINT32)(v[4] | v[5] << 8);
            if (fs && fs < a->frame) a->frame = fs;
            a->pn = 1;
        }
    } else if (type == MUX_MSC && len >= 2) {
        if (cmd) rf_mux(a, MUX_MSC, false, v, (UINT8)len);      /* (its modem status: acknowledged) */
        else a->msc = true;
    } else if (cmd) {                                           /* anything else: not supported */
        UINT8 t = p[0];
        rf_mux(a, MUX_NSC, false, &t, 1);
    }
}

/* A frame from the headset's RFCOMM (on the transport's thread) */
static void rf_rx(void *ctx, const UINT8 *p, UINT32 n)
{
    BthAudio *a = ctx;
    if (n < 4) return;
    UINT8 dlci = p[0] >> 2, ctl = p[1] & (UINT8)~RF_PF;
    bool pf = p[1] & RF_PF;
    UINT32 k = 2, len = p[2] >> 1;
    if (!(p[2] & 1)) { if (n < 5) return; len |= (UINT32)p[3] << 7; k++; }
    k++;
    switch (ctl) {
    case RF_UA:
        if (dlci == 0) a->mux_ua = 1;
        else if (dlci == a->dlci) a->dlc_ua = 1;
        break;
    case RF_DM:
        if (dlci == 0) a->mux_ua = -1;
        else if (dlci == a->dlci) { a->dlc_ua = -1; a->hfp_up = false; }
        break;
    case RF_DISC:
        rf_send(a, dlci, RF_UA | RF_PF, false, -1, NULL, 0);
        if (dlci == 0 || dlci == a->dlci) { a->dlc_ua = -1; a->hfp_up = false; }
        break;
    case RF_SABM:                                               /* (a channel of its own: none here) */
        rf_send(a, dlci, RF_DM | RF_PF, false, -1, NULL, 0);
        break;
    case RF_UIH:
        if (pf && a->cfc && dlci) {                             /* credits for us */
            if (k >= n) return;
            a->tx_credits += p[k++];
        }
        if (k + len > n) return;
        if (dlci == 0) { rf_mux_in(a, p + k, len); break; }
        if (dlci != a->dlci || !len) break;
        if (a->cfc) a->rx_credits--;
        nt_KeWaitForSingleObject(&a->at_lock, 0, 0, 0, NULL);
        for (UINT32 i = 0; i < len && a->at_len < sizeof(a->at); i++) a->at[a->at_len++] = (char)p[k + i];
        nt_KeSetEvent(&a->at_lock, 0, 0);
        break;
    }
}

/* Text on the hands-free DLC (as many frames as it takes, each when there
 * is a credit for it) */
static NT_STATUS rf_write(BthAudio *a, const char *s)
{
    UINT32 n = (UINT32)strlen(s);
    for (UINT32 off = 0; off < n; ) {
        UINT32 k = n - off < a->frame ? n - off : a->frame;
        if (a->cfc && !WAIT(a, a->tx_credits > 0 || !a->hfp_up, 2000)) return NT_STATUS_UNSUCCESSFUL;
        if (a->cfc) __atomic_fetch_sub(&a->tx_credits, 1, __ATOMIC_ACQ_REL);
        int give = -1;
        if (a->cfc && a->rx_credits <= CREDITS / 2) { give = CREDITS - a->rx_credits; a->rx_credits = CREDITS; }
        NT_STATUS st = rf_send(a, a->dlci, RF_UIH, true, give, (const UINT8 *)s + off, k);
        if (!NT_SUCCESS(st)) return st;
        off += k;
    }
    return NT_STATUS_SUCCESS;
}

static void reply(BthAudio *a, const char *text)
{
    char b[300];
    ksnprintf(b, sizeof(b), "\r\n%s\r\n", text);
    rf_write(a, b);
}

/* One AT command from the headset, answered as an audio gateway with no
 * telephone: its features, the indicators (service, no call, full signal,
 * not roaming, battery full) and event reporting set up the service level
 * connection; the rest are taken or refused */
static void at_command(BthAudio *a, const char *c)
{
    static const char *const ok[] = { "AT+BAC=", "AT+VGS=", "AT+VGM=", "AT+CLIP=", "AT+CCWA=", "AT+NREC=", "AT+CMEE=",
                                      "AT+BIA=", "AT+XAPL=", "AT+IPHONEACCEV=", "AT+COPS=", "AT+CLCC", "AT+BTRH?", "AT+CNUM",
                                      "AT+XEVENT=", "AT+BCC" };
    if (!strncmp(c, "AT+BRSF=", 8)) {
        reply(a, "+BRSF: 0");
    } else if (!strcmp(c, "AT+CIND=?")) {
        reply(a, "+CIND: (\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),"
                 "(\"signal\",(0-5)),(\"roam\",(0,1)),(\"battchg\",(0-5))");
    } else if (!strcmp(c, "AT+CIND?")) {
        reply(a, "+CIND: 1,0,0,0,5,0,5");
    } else if (!strncmp(c, "AT+CMER=", 8)) {
        reply(a, "OK");
        if (!a->slc) kprintf("[BTHAUD] %s: Hands-Free service level connection up\n", a->name);
        a->slc = true;
        return;
    } else if (!strcmp(c, "AT+CHLD=?")) {
        reply(a, "+CHLD: (0,1,2,3)");
    } else if (!strcmp(c, "AT+COPS?")) {
        reply(a, "+COPS: 0,0,\"NovaOS\"");
    } else {
        bool known = false;
        for (UINT32 i = 0; i < sizeof(ok) / sizeof(ok[0]) && !known; i++) known = !strncmp(c, ok[i], strlen(ok[i]));
        reply(a, known ? "OK" : "ERROR");
        return;
    }
    reply(a, "OK");
}

/* The AT commands that came, each answered; and credits given back */
static void hfp_poll(BthAudio *a)
{
    for (;;) {
        char line[256];
        int n = -1;
        nt_KeWaitForSingleObject(&a->at_lock, 0, 0, 0, NULL);
        for (UINT32 i = 0; i < a->at_len; i++)
            if (a->at[i] == '\r') {
                UINT32 s = 0;
                while (s < i && (a->at[s] == '\n' || a->at[s] == ' ')) s++;
                n = (int)(i - s);
                memcpy(line, a->at + s, (UINT32)n);
                line[n] = 0;
                a->at_len -= i + 1;
                memmove(a->at, a->at + i + 1, a->at_len);
                break;
            }
        if (n < 0 && a->at_len == sizeof(a->at)) a->at_len = 0;   /* (no end in sight: dropped) */
        nt_KeSetEvent(&a->at_lock, 0, 0);
        if (n < 0) break;
        if (n) at_command(a, line);
    }
    if (a->cfc && a->hfp_up && a->rx_credits <= CREDITS / 2) {
        int give = CREDITS - a->rx_credits;
        a->rx_credits = CREDITS;
        rf_send(a, a->dlci, RF_UIH, true, give, NULL, 0);
    }
}

/* RFCOMM to the headset's Hands-Free channel, then its service level connection */
static bool hfp_connect(BthAudio *a)
{
    a->dlci = (UINT8)(a->chan << 1);                            /* (the server's channel, direction 0: we started it) */
    a->mux_ua = a->dlc_ua = a->pn = 0;
    a->msc = a->slc = a->hfp_up = a->cfc = false;
    a->tx_credits = a->rx_credits = 0;
    a->at_len = 0;
    a->rfc = BthChanOpen(a->r, a->d, PSM_RFCOMM, rf_rx, a);
    if (!a->rfc) return false;
    UINT32 mtu = BthChanMtu(a->rfc);
    a->frame = mtu > 132 ? 127 : mtu - 5;
    rf_send(a, 0, RF_SABM | RF_PF, true, -1, NULL, 0);
    if (!WAIT(a, a->mux_ua, 5000) || a->mux_ua < 0) { kprintf("[BTHAUD] %s: RFCOMM refused\n", a->name); return false; }
    UINT8 pn[8] = { a->dlci, 0xF0, 7, 0, (UINT8)a->frame, (UINT8)(a->frame >> 8), 0, CREDITS };
    a->rx_credits = CREDITS;
    rf_mux(a, MUX_PN, true, pn, 8);
    if (!WAIT(a, a->pn, 3000)) a->rx_credits = 0;               /* (no answer: no flow control) */
    rf_send(a, a->dlci, RF_SABM | RF_PF, true, -1, NULL, 0);
    if (!WAIT(a, a->dlc_ua, 5000) || a->dlc_ua < 0) {
        kprintf("[BTHAUD] %s: RFCOMM channel %u refused\n", a->name, a->chan);
        return false;
    }
    a->hfp_up = true;
    UINT8 msc[2] = { (UINT8)(a->dlci << 2 | 3), 0x8D };          /* (ready: RTC, RTR, DV) */
    rf_mux(a, MUX_MSC, true, msc, 2);
    for (int i = 0; i < 1000 && !a->slc && a->hfp_up && !a->stop; i++) {
        hfp_poll(a);
        sleep_ms(10);
    }
    if (!a->slc) { kprintf("[BTHAUD] %s: no Hands-Free service level connection\n", a->name); return false; }
    kprintf("[BTHAUD] %s: Hands-Free on RFCOMM channel %u (%s)\n", a->name, a->chan,
            a->cfc ? "credit-based flow control" : "no flow control");
    return true;
}

/* ---- AVDTP ---- */

static void av_send(BthAudio *a, UINT8 label, UINT8 type, UINT8 sig, const UINT8 *p, UINT32 n)
{
    UINT8 b[64];
    if (n > sizeof(b) - 2) return;
    b[0] = (UINT8)(label << 4 | type);
    b[1] = sig;
    memcpy(b + 2, p, n);
    BthChanSend(a->r, a->sig, b, 2 + n);
}

/* Our own stream end point (SEID 1, an SBC source), for a headset that asks */
static UINT32 our_caps(UINT8 *o)
{
    static const UINT8 caps[] = { 0x01, 0x00, 0x07, 0x06, 0x00, 0x00, 0x30, 0xFF, 0x02, 53 };
    memcpy(o, caps, sizeof(caps));
    return sizeof(caps);
}

/* A signalling message from the headset (on the transport's thread) */
static void av_rx(void *ctx, const UINT8 *p, UINT32 n)
{
    BthAudio *a = ctx;
    if (n < 2) return;
    UINT8 label = p[0] >> 4, type = p[0] & 3, sig = p[1] & 0x3F;
    if ((p[0] >> 2 & 3) != 0) return;                           /* (fragmented messages: none expected) */
    if (type == 0) {                                            /* its command */
        UINT8 b[32];
        switch (sig) {
        case AV_DISCOVER:
            b[0] = 1 << 2;
            b[1] = 0 << 4 | 0 << 3;                             /* (audio, source) */
            av_send(a, label, 2, sig, b, 2);
            return;
        case AV_GET_CAPS:
        case AV_GET_ALL:
            av_send(a, label, 2, sig, b, our_caps(b));
            return;
        case AV_SUSPEND:
            a->streaming = false;
            av_send(a, label, 2, sig, NULL, 0);
            return;
        case AV_CLOSE:
        case AV_ABORT:
            a->streaming = false;
            a->a2dp_up = false;
            av_send(a, label, 2, sig, NULL, 0);
            return;
        case AV_DELAY:
        case AV_START:
            av_send(a, label, 2, sig, NULL, 0);
            return;
        }
        av_send(a, label, 1, sig, NULL, 0);                     /* (general reject) */
        return;
    }
    if (a->want == ((UINT32)label << 8 | sig) && !a->got) {
        UINT32 k = n - 2 < sizeof(a->resp) ? n - 2 : sizeof(a->resp);
        memcpy(a->resp, p + 2, k);
        a->resp_len = k;
        __atomic_store_n(&a->got, type, __ATOMIC_RELEASE);
    }
}

/* Command @sig, waiting for its answer: true if accepted (its parameters in a->resp) */
static bool av_cmd(BthAudio *a, UINT8 sig, const UINT8 *p, UINT32 n)
{
    a->label = (UINT8)((a->label + 1) & 15);
    a->got = 0;
    a->resp_len = 0;
    a->want = (UINT32)a->label << 8 | sig;
    av_send(a, a->label, 0, sig, p, n);
    bool ok = WAIT(a, __atomic_load_n(&a->got, __ATOMIC_ACQUIRE) != 0, 3000) && a->got == 2;
    a->want = 0;
    if (!ok) kprintf("[BTHAUD] %s: AVDTP signal %u %s\n", a->name, sig, a->got ? "rejected" : "not answered");
    return ok;
}

static const char *const g_modes[] = { "mono", "dual channel", "stereo", "joint stereo" };

/* The headset's SBC sink: found, configured, opened */
static bool a2dp_connect(BthAudio *a)
{
    a->streaming = a->a2dp_up = false;
    a->sig = BthChanOpen(a->r, a->d, PSM_AVDTP, av_rx, a);
    if (!a->sig || !av_cmd(a, AV_DISCOVER, NULL, 0)) return false;
    UINT8 seps[32];
    UINT32 nseps = a->resp_len / 2 < 16 ? a->resp_len / 2 : 16;
    memcpy(seps, a->resp, nseps * 2);
    for (UINT32 i = 0; i < nseps; i++) {
        UINT8 seid = seps[2 * i] >> 2;
        if ((seps[2 * i] & 2) || (seps[2 * i + 1] >> 4) != 0 || !(seps[2 * i + 1] & 8)) continue;   /* (in use, not audio, a source) */
        UINT8 q = (UINT8)(seid << 2);
        if (!av_cmd(a, AV_GET_CAPS, &q, 1)) continue;
        const UINT8 *c = NULL;
        for (UINT32 o = 0; o + 2 <= a->resp_len && o + 2 + a->resp[o + 1] <= a->resp_len; o += 2u + a->resp[o + 1])
            if (a->resp[o] == 0x07 && a->resp[o + 1] >= 6 && a->resp[o + 2] >> 4 == 0 && a->resp[o + 3] == 0) c = a->resp + o + 4;
        if (!c) continue;                                       /* (no SBC) */
        SbcEncoder *e = &a->sbc;
        memset(e, 0, sizeof(*e));
        e->rate = c[0] & 0x10 ? 48000 : c[0] & 0x20 ? 44100 : 0;
        e->mode = c[0] & 0x01 ? SBC_JOINT : c[0] & 0x02 ? SBC_STEREO : c[0] & 0x08 ? SBC_MONO : 0xFF;
        e->blocks = c[1] & 0x10 ? 16 : c[1] & 0x20 ? 12 : c[1] & 0x40 ? 8 : 4;
        e->subbands = c[1] & 0x04 ? 8 : 0;
        e->snr = c[1] & 0x01 ? 0 : 1;
        e->bitpool = c[3] < 53 ? c[3] : 53;
        if (e->bitpool < c[2] || e->mode == 0xFF || !SbcInit(e)) {
            kprintf("[BTHAUD] %s: SEID %u: no SBC this source can make (%02x %02x %u-%u)\n", a->name, seid, c[0], c[1], c[2], c[3]);
            continue;
        }
        UINT8 cfg[12] = { (UINT8)(seid << 2), 1 << 2, 0x01, 0x00, 0x07, 0x06, 0x00, 0x00,
                          (UINT8)((e->rate == 48000 ? 0x10 : 0x20) | (e->mode == SBC_JOINT ? 0x01 : e->mode == SBC_STEREO ? 0x02 : 0x08)),
                          (UINT8)((e->blocks == 16 ? 0x10 : e->blocks == 12 ? 0x20 : e->blocks == 8 ? 0x40 : 0x80) | 0x04 |
                                  (e->snr ? 0x02 : 0x01)), e->bitpool, e->bitpool };
        if (!av_cmd(a, AV_SET_CONFIG, cfg, sizeof(cfg)) || !av_cmd(a, AV_OPEN, &q, 1)) return false;
        a->media = BthChanOpen(a->r, a->d, PSM_AVDTP, NULL, NULL);
        if (!a->media) return false;
        a->acp = seid;
        a->a2dp_up = true;
        kprintf("[BTHAUD] %s: A2DP sink SEID %u: SBC %u.%u kHz %s, %u blocks, 8 subbands, %s, bitpool %u, %u-byte frames\n",
                a->name, seid, e->rate / 1000, e->rate % 1000 / 100, g_modes[e->mode], e->blocks, e->snr ? "SNR" : "loudness",
                e->bitpool, SbcFrameLength(e));
        return true;
    }
    kprintf("[BTHAUD] %s: no SBC audio sink\n", a->name);
    return false;
}

/* The media packet so far, sent (RTP, then SBC's header: its frame count) */
static void a2dp_flush(BthAudio *a)
{
    if (!a->pkt_frames) return;
    UINT8 *p = a->pkt;
    p[0] = 0x80;                                                /* (RTP version 2) */
    p[1] = 0x60;                                                /* (payload type 96) */
    p[2] = (UINT8)(a->seq >> 8); p[3] = (UINT8)a->seq;
    p[4] = (UINT8)(a->ts >> 24); p[5] = (UINT8)(a->ts >> 16); p[6] = (UINT8)(a->ts >> 8); p[7] = (UINT8)a->ts;
    p[8] = p[9] = p[10] = 0; p[11] = 1;                         /* (SSRC) */
    p[12] = (UINT8)a->pkt_frames;
    BthChanSend(a->r, a->media, p, a->pkt_len);
    a->seq++;
    a->ts += a->pkt_frames * SbcFrameSamples(&a->sbc);
    a->pkt_len = 13;
    a->pkt_frames = 0;
}

static void a2dp_frame(BthAudio *a)
{
    UINT32 len = SbcFrameLength(&a->sbc), mtu = BthChanMtu(a->media);
    if (mtu > MAX_PKT) mtu = MAX_PKT;
    if (a->pkt_len + len > mtu || a->pkt_frames == 15) a2dp_flush(a);
    SbcEncode(&a->sbc, a->pend, a->pkt + a->pkt_len);
    a->pkt_len += len;
    a->pkt_frames++;
}

/* ---- the mixer's devices ---- */

static UINT32 apos(void *ctx) { return ((BthAudio *)ctx)->apos; }
static UINT32 hpos(void *ctx) { return ((BthAudio *)ctx)->hpos; }
static UINT32 ipos(void *ctx) { return ((BthAudio *)ctx)->ipos; }
static void   irun(void *ctx, bool on) { ((BthAudio *)ctx)->rec = on; }   /* (the mixer's lock held: no waiting) */

/* The microphone's samples, from the voice link (on the transport's thread) */
static void sco_rx(void *ctx, const UINT8 *data, UINT32 len)
{
    BthAudio *a = ctx;
    UINT32 at = a->ipos / 2;
    for (UINT32 i = 0; i + 1 < len; i += 2) {
        a->iring[at] = (INT16)(data[i] | data[i + 1] << 8);
        at = (at + 1) % HF_FRAMES;
    }
    __atomic_store_n(&a->ipos, at * 2, __ATOMIC_RELEASE);
}

static void attach(BthAudio *a)
{
    a->t0 = now_ms();
    a->adone = a->hdone = 0;
    a->apos = a->hpos = a->ipos = 0;
    a->npend = a->nvoice = 0;
    a->pkt_len = 13;
    a->pkt_frames = 0;
    a->a_heard = a->h_heard = 0;
    memset(a->aring, 0, A2DP_FRAMES * 4);
    memset(a->hring, 0, HF_FRAMES * 4);
    memset(a->iring, 0, HF_FRAMES * 2);
    /* the hands-free devices first, so the stereo output is the default
     * one (Windows' choice too: voice is the communications device) */
    if (a->hfp_up && !a->hout_on) a->hout_on = AudioOutputAttach(&a->hout);
    if (a->hfp_up && !a->hin_on) a->hin_on = AudioInputAttach(&a->hin);
    if (a->a2dp_up && !a->aout_on) a->aout_on = AudioOutputAttach(&a->aout);
}

static void detach(BthAudio *a)
{
    if (a->aout_on) AudioOutputDetach(&a->aout);
    if (a->hout_on) AudioOutputDetach(&a->hout);
    if (a->hin_on) AudioInputDetach(&a->hin);
    a->aout_on = a->hout_on = a->hin_on = false;
}

/* The frames an output's device has played by now (its rate since the
 * devices came): how many to take from its ring this time */
static UINT32 due(BthAudio *a, UINT64 now, UINT32 rate, UINT64 *done)
{
    UINT64 want = (now - a->t0) * rate / 1000;
    if (want - *done > rate / 4) *done = want - rate / 50;      /* (held up: what fell behind is gone) */
    UINT32 n = (UINT32)(want - *done);
    *done = want;
    return n;
}

static void a2dp_pump(BthAudio *a, UINT64 now)
{
    UINT32 n = due(a, now, a->sbc.rate, &a->adone), pos = a->apos / 4;
    bool sound = false;
    for (UINT32 i = 0; i < n; i++) {
        INT16 *f = a->aring + 2 * pos;
        sound |= f[0] || f[1];
        if (a->streaming) {
            a->pend[2 * a->npend] = f[0];
            a->pend[2 * a->npend + 1] = f[1];
            if (++a->npend == SbcFrameSamples(&a->sbc)) { a2dp_frame(a); a->npend = 0; }
        }
        pos = (pos + 1) % A2DP_FRAMES;
    }
    __atomic_store_n(&a->apos, pos * 4, __ATOMIC_RELEASE);
    if (a->streaming) a2dp_flush(a);
    if (sound) a->a_heard = now;
    UINT8 q = (UINT8)(a->acp << 2);
    if (sound && !a->streaming) {
        if (av_cmd(a, AV_START, &q, 1)) {
            a->streaming = true;
            a->npend = 0;
            kprintf("[BTHAUD] %s: A2DP stream started\n", a->name);
        }
    } else if (!sound && a->streaming && now - a->a_heard > IDLE_MS) {
        a->streaming = false;
        if (av_cmd(a, AV_SUSPEND, &q, 1)) kprintf("[BTHAUD] %s: A2DP stream suspended (%u packets)\n", a->name, a->seq);
    }
}

static void hfp_pump(BthAudio *a, UINT64 now)
{
    hfp_poll(a);
    UINT32 n = due(a, now, HF_RATE, &a->hdone), pos = a->hpos / 4;
    bool sound = false, up = BthScoIsOpen(a->d);
    for (UINT32 i = 0; i < n; i++) {
        INT16 *f = a->hring + 2 * pos;
        sound |= f[0] || f[1];
        if (up) {
            a->voice[a->nvoice++] = (INT16)(((INT32)f[0] + f[1]) / 2);
            if (a->nvoice == SCO_CHUNK) { BthScoSend(a->r, a->d, (const UINT8 *)a->voice, sizeof(a->voice)); a->nvoice = 0; }
        }
        pos = (pos + 1) % HF_FRAMES;
    }
    __atomic_store_n(&a->hpos, pos * 4, __ATOMIC_RELEASE);
    if (sound) a->h_heard = now;
    if ((sound || a->rec) && !up) {
        if (NT_SUCCESS(BthScoOpen(a->r, a->d, sco_rx, a))) {
            a->nvoice = 0;
            kprintf("[BTHAUD] %s: voice link open (%s)\n", a->name, a->rec ? "recording" : "playing");
        }
        a->hdone = (now_ms() - a->t0) * HF_RATE / 1000;          /* (what played while it opened: gone) */
    } else if (up && !sound && !a->rec && now - a->h_heard > IDLE_MS) {
        BthScoClose(a->r, a->d);
        kprintf("[BTHAUD] %s: voice link closed\n", a->name);
    }
}

/* ---- the headset's thread ---- */

static void links_close(BthAudio *a)
{
    if (BthScoIsOpen(a->d)) BthScoClose(a->r, a->d);
    if (a->rfc && a->hfp_up) rf_send(a, a->dlci, RF_DISC | RF_PF, true, -1, NULL, 0);
    BthChanClose(a->r, a->rfc);
    BthChanClose(a->r, a->media);
    BthChanClose(a->r, a->sig);
    a->rfc = a->media = a->sig = NULL;
    a->hfp_up = a->slc = a->a2dp_up = a->streaming = false;
}

static bool links_open(BthAudio *a)
{
    if (!NT_SUCCESS(BthDevLink(a->r, a->d))) return false;
    bool any = false;
    if (a->a2dp) any |= a2dp_connect(a);
    if (a->hfp && !a->stop) any |= hfp_connect(a);
    if (!a->hfp_up && a->rfc) { BthChanClose(a->r, a->rfc); a->rfc = NULL; }
    if (!a->a2dp_up && (a->sig || a->media)) {
        BthChanClose(a->r, a->media);
        BthChanClose(a->r, a->sig);
        a->media = a->sig = NULL;
    }
    return any;
}

static bool links_lost(BthAudio *a)
{
    return !BthDevUp(a->r, a->d) || (a->rfc && (!BthChanIsOpen(a->rfc) || !a->hfp_up)) ||
           (a->sig && (!BthChanIsOpen(a->sig) || !BthChanIsOpen(a->media) || !a->a2dp_up));
}

static void run(void *arg)
{
    BthAudio *a = arg;
    UINT64 retry = 0;
    bool up = false;
    while (!a->stop) {
        UINT64 now = now_ms();
        if (!up) {
            if (now < retry) { sleep_ms(100); continue; }
            if (!links_open(a)) {
                links_close(a);
                retry = now_ms() + RETRY_MS;
                continue;
            }
            up = true;
            attach(a);
            continue;
        }
        if (a->a2dp_up) a2dp_pump(a, now);
        if (a->hfp_up) hfp_pump(a, now);
        if (links_lost(a)) {
            kprintf("[BTHAUD] %s: disconnected; trying again in %u s\n", a->name, RETRY_MS / 1000);
            detach(a);
            links_close(a);
            up = false;
            retry = now_ms() + RETRY_MS;
            continue;
        }
        UINT64 spent = now_ms() - now;
        sleep_ms(spent < TICK_MS ? TICK_MS - (UINT32)spent : 1);
    }
    detach(a);
    links_close(a);
    nt_KeSetEvent(&a->done, 0, 0);
}

BthAudio *BthAudioStart(BthRadio *r, BthDev *d, const char *name, bool hfp, UINT8 channel, bool a2dp)
{
    static bool crc_ready;
    if (!crc_ready) { crc_init(); crc_ready = true; }
    BthAudio *a = kzalloc(sizeof(*a));
    if (!a) return NULL;
    a->r = r;
    a->d = d;
    a->hfp = hfp;
    a->chan = channel;
    a->a2dp = a2dp;
    strncpy(a->name, name, sizeof(a->name) - 1);
    nt_KeInitializeEvent(&a->done, NT_NotificationEvent, 0);
    nt_KeInitializeEvent(&a->at_lock, NT_SynchronizationEvent, 1);
    a->aring = kmalloc(A2DP_FRAMES * 4);
    a->hring = kmalloc(HF_FRAMES * 4);
    a->iring = kmalloc(HF_FRAMES * 2);
    ksnprintf(a->aname, sizeof(a->aname), "Headphones (%s)", name);
    ksnprintf(a->hname, sizeof(a->hname), "Headset (%s Hands-Free)", name);
    ksnprintf(a->mname, sizeof(a->mname), "Headset (%s Hands-Free)", name);
    ksnprintf(a->akey, sizeof(a->akey), "BTHA2DP %p", (void *)d);
    ksnprintf(a->hkey, sizeof(a->hkey), "BTHHFP %p", (void *)d);
    a->aout = (AudioOutput){ a->aname, a->akey, a->aring, A2DP_FRAMES * 4, 0, apos, a };
    a->hout = (AudioOutput){ a->hname, a->hkey, a->hring, HF_FRAMES * 4, HF_RATE, hpos, a };
    a->hin = (AudioInput){ a->mname, a->hkey, a->iring, HF_FRAMES * 2, HF_RATE, 1, ipos, irun, a };
    if (!a->aring || !a->hring || !a->iring || !sched_create_thread_ex("bth-audio", run, a, PRIO_DEVICE_IO, 16 * 1024)) {
        kfree(a->aring);
        kfree(a->hring);
        kfree(a->iring);
        kfree(a);
        return NULL;
    }
    kprintf("[BTHAUD] %s:%s%s\n", name, hfp ? " Hands-Free" : "", a2dp ? " Audio Sink" : "");
    return a;
}

void BthAudioStop(BthAudio *a)
{
    if (!a) return;
    a->stop = true;
    nt_KeWaitForSingleObject(&a->done, 0, 0, 0, NULL);
    kfree(a->aring);
    kfree(a->hring);
    kfree(a->iring);
    kfree(a);
}
