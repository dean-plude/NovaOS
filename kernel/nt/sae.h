/*
 * sae.h — the station side of SAE, the password authentication of
 * WPA3-Personal (IEEE 802.11-2020 12.4: Simultaneous Authentication of
 * Equals), over the NIST P-256 curve (group 19) with hunting and pecking
 *
 * Both sides commit (a scalar and an element derived from the password,
 * both MAC addresses and a random secret) and then confirm (an HMAC over
 * both commits with a key only the same password gives); the pairwise
 * master key (PMK) that comes out of it is what the 4-way handshake (wpa.h,
 * AKM 00-0f-ac:8) starts from.  On Windows the OS does this and the Wi-Fi
 * driver only carries the authentication frames (wdi.h: WDI's
 * SAE_AUTH_PARAMS messages); kernel/nt/wdi.c does the same with this.
 */
#pragma once

#include "../include/types.h"

#define SAE_GROUP       19          /* (the one group: NIST P-256, every WPA3 access point has it) */
#define SAE_PRIME_LEN   32
#define SAE_TOKEN_MAX   64
#define SAE_PMKID_LEN   16

typedef struct {
    /* the commit (SaeCommit): its scalar and element, and the secrets behind them */
    UINT8  scalar[SAE_PRIME_LEN], element[2 * SAE_PRIME_LEN];
    UINT8  rand[SAE_PRIME_LEN];
    UINT8  pwe[2 * SAE_PRIME_LEN];          /* (the password element: x, y) */
    /* the access point's commit, and what the two make (SaeProcessCommit) */
    UINT8  peer_scalar[SAE_PRIME_LEN], peer_element[2 * SAE_PRIME_LEN];
    UINT8  kck[32], pmk[32], pmkid[SAE_PMKID_LEN];
    bool   have_keys;
    UINT16 send_confirm;                    /* (the last confirm's: SaeConfirm counts up) */
    /* an anti-clogging token the access point asked the next commit to carry */
    UINT8  token[SAE_TOKEN_MAX];
    UINT8  token_len;
    const char *why;                        /* (why the last step failed) */
} SaeState;

/* The commit for @password between station @sta and access point @ap:
 * the password element (hunting and pecking, always 40 rounds), a random
 * secret and mask, the scalar and element.  False if no element was
 * found (a password, in theory) or Mbed TLS failed. */
bool SaeCommit(SaeState *s, const char *password, const UINT8 sta[6], const UINT8 ap[6]);
/* The same with the secret @rand and @mask given (SAE_PRIME_LEN bytes each,
 * big-endian): the known-answer test's (tests/host/sae_handshake.c) */
bool SaeCommitWith(SaeState *s, const char *password, const UINT8 sta[6], const UINT8 ap[6], const UINT8 *rand,
                   const UINT8 *mask);
/* The access point's commit (@scalar, @element: big-endian, as in its
 * frame) checked (in range, on the curve, not the station's own sent back)
 * and the shared secret's KCK, PMK and PMKID derived */
bool SaeProcessCommit(SaeState *s, const UINT8 *scalar, const UINT8 *element);
/* The next confirm: its send-confirm (*@send_confirm) and @confirm */
void SaeConfirm(SaeState *s, UINT16 *send_confirm, UINT8 confirm[32]);
/* Whether the access point's confirm (@send_confirm, @confirm) proves it
 * has the same password */
bool SaeVerifyConfirm(SaeState *s, UINT16 send_confirm, const UINT8 confirm[32]);
/* Forget every secret */
void SaeClear(SaeState *s);

/* An SAE authentication frame from the access point, as a driver passes it
 * (WDI_TLV_SAE_COMMIT_RESPONSE or _CONFIRM_RESPONSE: the frame body from
 * its authentication algorithm, or from its group or send-confirm; a frame
 * with its 24-byte MAC header is taken too).  The fields it has: */
typedef struct {
    UINT16 seq;                             /* 1 commit, 2 confirm (0: the body had no header to say) */
    UINT16 status;                          /* its status code (76: anti-clogging token required, 77: group refused) */
    UINT16 group;
    const UINT8 *token;                     /* (a commit whose status is 76) */
    UINT32 token_len;
    const UINT8 *scalar, *element;          /* (a commit) */
    UINT16 send_confirm;                    /* (a confirm) */
    const UINT8 *confirm;
} SaeFrame;
#define SAE_STATUS_ANTI_CLOGGING 76
#define SAE_STATUS_GROUP_REFUSED 77
bool SaeParseCommit(const UINT8 *f, UINT32 n, SaeFrame *out);
bool SaeParseConfirm(const UINT8 *f, UINT32 n, SaeFrame *out);
