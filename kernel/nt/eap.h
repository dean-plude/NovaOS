/*
 * eap.h — the station side of WPA2-Enterprise's sign-in: IEEE 802.1X's
 * EAP over LAN (EAPOL) to the access point, which relays EAP to its
 * authentication server.  NovaOS's EAP peer (RFC 3748) speaks PEAP
 * version 0 (Microsoft's [MS-PEAP], the method Windows uses by default)
 * with EAP-MSCHAPv2 inside the tunnel (RFC 2759's MS-CHAPv2 over
 * [MS-CHAP]'s EAP framing): a user name and password, checked by the
 * server, and the server's own reply checked against the same password.
 * The tunnel is TLS 1.2 (net/tls.h's EAP sessions, Mbed TLS): the
 * server's certificate must chain to a trusted root (certutil -addstore
 * root), as Windows wants by default before it sends a password.
 *
 * EAP-TLS (RFC 5216, Windows' "smart card or other certificate") signs in
 * with a certificate instead: the same TLS 1.2 handshake in the same
 * fragments, the server asking for the station's certificate, which comes
 * from the personal store with its private key (certutil -importpfx;
 * net/tls.h's TlsPersonalFind) and is checked by the server; no password
 * and nothing inside the tunnel.  The peer asks for the method it has: a
 * Nak names EAP-TLS when the server offers PEAP and the user has a
 * certificate, PEAP when the server offers EAP-TLS and the user a password.
 *
 * Once the server says EAP-Success, the tunnel's keying material gives
 * the MSK ("client EAP encryption", RFC 5216 2.3's PRF over the TLS
 * master secret and both randoms) and its first 32 bytes are the PMK the
 * 4-way handshake starts from (wpa.h, AKM 00-0f-ac:1).
 *
 * kernel/nt/wdi.c carries the EAPOL frames (Ethertype 0x888E, unencrypted
 * under the EAPOL exemption) between this and the access point.
 */
#pragma once

#include "../include/types.h"

#define EAP_MSK_LEN     64
#define EAP_FRAME_MAX   1600        /* (an EAPOL frame either way: EAP fragments are at most EAP_FRAGMENT) */
#define EAP_FRAGMENT    1024        /* (the TLS bytes in one PEAP fragment the station sends) */
#define EAP_NAME_MAX    128
#define EAP_METHOD_TLS  13          /* (EAP-TLS, RFC 5216) */
#define EAP_METHOD_PEAP 25          /* (PEAP, [MS-PEAP]) */

typedef struct TlsEap TlsEap;

typedef struct {
    /* set before the sign-in (EapStart) */
    char   identity[EAP_NAME_MAX];  /* the user name ("DOMAIN\user" or "user@realm"), also the outer identity */
    char   password[EAP_NAME_MAX];
    UINT8  method;                  /* EAP_METHOD_PEAP (a password) or EAP_METHOD_TLS (a certificate) */
    int    cert;                    /* EAP-TLS: the personal certificate (net/tls.h), else -1 */
    /* the sign-in's */
    TlsEap *tls;                    /* the TLS session: PEAP's tunnel, or EAP-TLS's handshake */
    UINT8  id;                      /* the last request's identifier */
    bool   started;                 /* a request came (no need for EAPOL-Start) */
    bool   tunnel;                  /* the TLS handshake finished */
    UINT8 *in;                      /* TLS bytes of a fragmented message still coming in */
    UINT32 in_len, in_total;
    UINT8 *out;                     /* TLS bytes still to go out, a fragment per request */
    UINT32 out_len, out_off;
    /* phase 2: MS-CHAPv2 inside the tunnel */
    UINT8  auth_challenge[16], peer_challenge[16], nt_response[24];
    bool   chap_sent, chap_ok;      /* sent the response; the server proved it knows the password too */
    bool   result_ok;               /* the server's Result TLV said success */
    /* the outcome */
    int    result;                  /* EAP_* below once done */
    UINT8  msk[EAP_MSK_LEN];
    char   server[96];              /* the server certificate's subject name */
    const char *why;                /* (why the last frame was dropped, or why it failed) */
} EapPeer;

/* What EapReceive did with an EAPOL frame */
enum {
    EAP_DROP,           /* not for the peer, or a check failed (peer->why) */
    EAP_REPLY,          /* send the reply */
    EAP_SUCCESS,        /* signed in: peer->msk is the MSK (no reply) */
    EAP_FAILURE,        /* the server refused, or the peer gave up (peer->result says why; maybe a reply) */
};
/* peer->result: */
#define EAP_RESULT_NONE        0
#define EAP_RESULT_OK          1
#define EAP_RESULT_BAD_PASSWORD 2   /* MS-CHAPv2 failure: the user name or password is wrong */
#define EAP_RESULT_UNTRUSTED   3    /* the server's certificate does not chain to a trusted root */
#define EAP_RESULT_FAILED      4    /* anything else (no common method, TLS failed, the server's proof wrong) */
#define EAP_RESULT_CERT_REFUSED 5   /* EAP-TLS: the server did not accept the station's certificate */

/* Start a sign-in as @identity with @password (false if either is empty
 * or too long) */
bool EapStart(EapPeer *p, const char *identity, const char *password);
/* Start an EAP-TLS sign-in as @identity with personal certificate @cert */
bool EapStartCert(EapPeer *p, const char *identity, int cert);
/* EAPOL-Start (an EAPOL frame in @out, its length), for when the access
 * point does not ask first */
UINT32 EapStartFrame(UINT8 *out);
/* One EAPOL frame (@frame, @len: from its EAPOL header) from the access
 * point: the reply in @reply (*@reply_len bytes, an EAPOL frame, up to
 * EAP_FRAME_MAX) */
int  EapReceive(EapPeer *p, const UINT8 *frame, UINT32 len, UINT8 *reply, UINT32 *reply_len);
/* Forget the password, the keys and the tunnel */
void EapClear(EapPeer *p);

/* MS-CHAPv2 (RFC 2759), for the peer and its tests: the NT-Response to
 * @auth_challenge for @user (no domain) and @password, and the
 * authenticator response ("S=" and 40 hex digits) a server that knows
 * the password sends back */
void MschapNtResponse(const UINT8 auth_challenge[16], const UINT8 peer_challenge[16], const char *user,
                      const char *password, UINT8 response[24]);
void MschapAuthenticatorResponse(const char *password, const UINT8 nt_response[24], const UINT8 peer_challenge[16],
                                 const UINT8 auth_challenge[16], const char *user, char out[43]);
/* MD4 (RFC 1320): MS-CHAPv2's password hash; Mbed TLS 3 no longer has it */
void Md4(const UINT8 *data, UINT32 len, UINT8 out[16]);
