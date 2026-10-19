/* The kernel's WPA3-Personal station (kernel/nt/sae.c over Mbed TLS, and
 * kernel/nt/wpa.c's SAE handshake) against wditest.sys's access point
 * (tools/drivertest/saeap.h and wpaap.h, their own P-256, SHA-256 and AES)
 * on the host: both against IEEE 802.11-2020 Annex J.10's SAE vector, then
 * SAE between the two, the WPA3 4-way handshake with its IGTK, a group
 * rekey, a wrong password, a reflected commit and the frame parsers.  It
 * prints values for tests/tools/test_sae_handshake.py to check. */
#include "../../kernel/nt/sae.c"            /* (first: it asks for Mbed TLS's private fields) */
#include "../../kernel/nt/wpa.c"
#include "../../tools/drivertest/saeap.h"
#include <assert.h>
#include <stdio.h>

void TlsEntropyOutput(UINT8 *out, size_t len) { memset(out, 0x5A, len); }   /* (the station's secrets: fixed here) */

static void hex(const char *name, const UINT8 *p, unsigned int n)
{
    printf("%s ", name);
    for (unsigned int i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
}

static void unhex(const char *s, UINT8 *out)
{
    for (int i = 0; s[2 * i]; i++) sscanf(s + 2 * i, "%2hhx", &out[i]);
}

/* Annex J.10 (as hostapd's src/common/common_module_tests.c, BSD, has it) */
static const UINT8 J_STA[6] = { 0x4d, 0x3f, 0x2f, 0xff, 0xe3, 0x87 }, J_AP[6] = { 0xa5, 0xd8, 0xaa, 0x95, 0x8e, 0x3c };
#define J_PW "mekmitasdigoat"
#define J_RAND "992465fd3daa3c60aa6565b7f62a2a7f2e12dd12f198faf4fbed89d7ff1ace94"
#define J_MASK "9507a90f777a044d6a0830b91ea3d5dd70bece44e1acffb86983b5e1bf9fb322"
#define J_COMMIT "2e2c0f0db52440ad146d967114ce005ce1eab0aa2c2e5c2871b774f6c2575c65d5ad9e00829707aa36ba8b859738fc96" \
    "1d08243505f47c035376d7ac4bc8d7b95083bf43827d0fc31ed778dd3671fd21a46d1091d64b6f9a1e1272621325dbe1"
#define J_PEER "591b96f3397fb945100848e7b550543b6720d88337ee93fc49fd6df7e08b5223e71b9bb048d3873f20556953a96c9153" \
    "6fd8ee6ca9b4a68a148b056a909be03e83ae208f60f8ef5537858074db06687032399862999b511e0a1552a5fea317c2"
#define J_KCK "1e733f6d9bd53256287304338831b09a39406d121017073a5c30db36f36cb81a"
#define J_PMK "4e4dfab1a2dd8ac1a91790f953faaa452ae5c6873ab75b63605ba663f8a7fe59"
#define J_PMKID "8747a600eea3f9f22475df58ca1e5498"

/* The station's RSN element for WPA3 (SAE, CCMP, MFP capable and required) and the access point's */
static const UINT8 RSN_SAE[] = { 48, 20, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac,
                                 8, 0xc0, 0x00 };

int main(void)
{
    /* known answers for saeap.h's own primitives: SHA-256 ("abc"), HMAC-SHA256 (RFC 4231 1), CMAC (RFC 4493 2) */
    UINT8 d[64], want[128], k[16];
    Sha256 c;
    sha256_init(&c); sha256_update(&c, (const u8 *)"abc", 3); sha256_final(&c, d);
    unhex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", want);
    assert(!memcmp(d, want, 32));
    UINT8 key[20];
    memset(key, 0x0b, 20);
    ap_hmac256(key, 20, (const u8 *)"Hi There", 8, 0, 0, d);
    unhex("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", want);
    assert(!memcmp(d, want, 32));
    unhex("2b7e151628aed2a6abf7158809cf4f3c", k);
    UINT8 m[16];
    unhex("6bc1bee22e409f96e93d7e117393172a", m);
    aes_cmac(k, m, 16, d);
    unhex("070a16b46b4d4144f79bdd9dd04a287c", want);
    assert(!memcmp(d, want, 16));
    aes_cmac(k, m, 0, d);
    unhex("bb1d6929e95937287fa37d129b756746", want);
    assert(!memcmp(d, want, 16));

    /* Annex J.10 (hunting and pecking: its commit holds the PWE's work): the kernel's station side, then the
     * access point's, each as the vector's first party */
    UINT8 rnd[32], mask[32], commit[96], peer[96];
    unhex(J_RAND, rnd); unhex(J_MASK, mask); unhex(J_COMMIT, commit); unhex(J_PEER, peer);
    static SaeState s;
    assert(SaeCommitWith(&s, J_PW, J_STA, J_AP, rnd, mask));
    assert(!memcmp(s.scalar, commit, 32) && !memcmp(s.element, commit + 32, 64));
    assert(SaeProcessCommit(&s, peer, peer + 32));
    unhex(J_KCK, want);
    assert(!memcmp(s.kck, want, 32));
    unhex(J_PMK, want);
    assert(!memcmp(s.pmk, want, 32));
    unhex(J_PMKID, want);
    assert(!memcmp(s.pmkid, want, 16));
    hex("j10_pmk", s.pmk, 32);
    static SaeAp a;
    assert(saeap_pwe(&a, J_PW, J_AP, J_STA) && !memcmp(a.pwe, s.pwe, 64));
    assert(saeap_commit(&a, rnd, mask));
    assert(!memcmp(a.scalar, commit, 32) && !memcmp(a.element, commit + 32, 64));
    assert(saeap_process(&a, peer, peer + 32));
    assert(!memcmp(a.pmk, s.pmk, 32) && !memcmp(a.kck, s.kck, 32) && !memcmp(a.pmkid, s.pmkid, 16));
    printf("annex j.10 ok\n");

    /* SAE between the two (the access point's secrets: other fixed numbers), then the WPA3 4-way handshake */
    static const UINT8 sta[6] = { 0x02, 0x4e, 0x6f, 0x76, 0x61, 0x00 }, ap[6] = { 0x02, 0x4e, 0x6f, 0x76, 0x61, 0x02 };
    for (int i = 0; i < 32; i++) { rnd[i] = (UINT8)(0x11 + i); mask[i] = (UINT8)(0x77 - i); }
    assert(SaeCommit(&s, "nova-wpa3-password", sta, ap));
    memset(&a, 0, sizeof(a));
    assert(saeap_pwe(&a, "nova-wpa3-password", ap, sta) && !memcmp(a.pwe, s.pwe, 64));
    assert(saeap_commit(&a, rnd, mask));
    assert(saeap_process(&a, s.scalar, s.element));
    assert(SaeProcessCommit(&s, a.scalar, a.element));
    UINT16 sc;
    UINT8 conf[32];
    SaeConfirm(&s, &sc, conf);
    assert(sc == 1 && saeap_verify(&a, sc, conf));
    saeap_confirm(&a, 1, 1, conf);
    assert(SaeVerifyConfirm(&s, 1, conf));
    assert(!memcmp(a.pmk, s.pmk, 32));
    hex("sae_pmk", s.pmk, 32);
    printf("sae ok\n");

    static WpaSupplicant w;
    static WpaAp wa;
    memset(&w, 0, sizeof(w));
    memset(&wa, 0, sizeof(wa));
    w.akm = WPA_AKM_SAE;
    w.mfp = true;
    memcpy(w.pmk, s.pmk, 32);
    memcpy(w.aa, ap, 6); memcpy(w.spa, sta, 6);
    memcpy(w.own_ie, RSN_SAE, w.own_ie_len = sizeof(RSN_SAE));
    memcpy(w.ap_ie, RSN_SAE, w.ap_ie_len = sizeof(RSN_SAE));
    wa.sae = 1;
    memcpy(wa.pmk, a.pmk, 32);
    memcpy(wa.aa, ap, 6); memcpy(wa.spa, sta, 6);
    wa.ap_ie = RSN_SAE; wa.ap_ie_len = sizeof(RSN_SAE); wa.sta_ie = RSN_SAE; wa.sta_ie_len = sizeof(RSN_SAE);
    for (int i = 0; i < 32; i++) wa.anonce[i] = (UINT8)(i * 7 + 1);
    for (int i = 0; i < 16; i++) { wa.gtk[i] = (UINT8)(0xA0 + i); wa.igtk[i] = (UINT8)(0xC0 + i); }
    wa.gtk_id = 1;
    wa.igtk_id = 4;
    wa.ipn[0] = 3;
    wa.rsc[0] = 5;
    WpaStart(&w);
    UINT8 f[512], rep[WPA_FRAME_MAX];
    UINT32 n, rl;
    n = ap_msg1(&wa, f);
    assert(WpaReceive(&w, f, n, rep, &rl) == WPA_REPLY);
    assert(ap_msg2(&wa, rep, rl));
    hex("sae_ptk", wa.ptk, 48);
    n = ap_msg3(&wa, f);
    assert(WpaReceive(&w, f, n, rep, &rl) == WPA_PAIRWISE);
    assert(ap_msg4(&wa, rep, rl));
    assert(!memcmp(WpaTk(&w), AP_TK(&wa), 16) && !memcmp(w.gtk, wa.gtk, 16) && w.have_igtk && w.igtk_id == 4 &&
           !memcmp(w.igtk, wa.igtk, 16) && w.igtk_ipn[0] == 3);
    for (int i = 0; i < 16; i++) { wa.gtk[i] = (UINT8)(0xB0 + i); wa.igtk[i] = (UINT8)(0xD0 + i); }
    wa.gtk_id = 2;
    wa.igtk_id = 5;
    n = ap_group1(&wa, f);
    assert(WpaReceive(&w, f, n, rep, &rl) == WPA_GROUP);
    assert(ap_group2(&wa, rep, rl));
    assert(w.gtk_id == 2 && !memcmp(w.gtk, wa.gtk, 16) && w.igtk_id == 5 && !memcmp(w.igtk, wa.igtk, 16));
    printf("wpa3 handshake ok\n");

    /* a WPA2 (version 2) frame on a WPA3 connection is refused */
    WpaStart(&w);
    wa.sae = 0;
    n = ap_msg1(&wa, f);
    assert(WpaReceive(&w, f, n, rep, &rl) == WPA_DROP);
    printf("version refused: %s\n", w.why);

    /* a wrong password: the commits go through, each side's confirm fails the other's check */
    assert(SaeCommit(&s, "not-the-password", sta, ap));
    memset(&a, 0, sizeof(a));
    assert(saeap_pwe(&a, "nova-wpa3-password", ap, sta) && saeap_commit(&a, rnd, mask));
    assert(saeap_process(&a, s.scalar, s.element) && SaeProcessCommit(&s, a.scalar, a.element));
    SaeConfirm(&s, &sc, conf);
    assert(!saeap_verify(&a, sc, conf));
    printf("wrong password refused: %s\n", a.why);
    saeap_confirm(&a, 1, 1, conf);
    assert(!SaeVerifyConfirm(&s, 1, conf));
    printf("wrong password at the station: %s\n", s.why);

    /* a reflected commit, an element off the curve, an out-of-range scalar */
    assert(SaeCommit(&s, "nova-wpa3-password", sta, ap));
    assert(!SaeProcessCommit(&s, s.scalar, s.element));
    printf("reflection refused: %s\n", s.why);
    memcpy(peer, a.scalar, 32);
    memcpy(peer + 32, a.element, 64);
    peer[95] ^= 1;
    assert(!SaeProcessCommit(&s, peer, peer + 32));
    printf("bad element refused: %s\n", s.why);
    memset(peer, 0xFF, 32);
    assert(!SaeProcessCommit(&s, peer, a.element));
    printf("bad scalar refused: %s\n", s.why);

    /* the frames a driver passes up: with and without their headers, an anti-clogging request, a refusal */
    UINT8 fr[24 + 6 + 2 + 96 + 8];
    SaeFrame sf;
    memset(fr, 0, sizeof(fr));
    fr[24] = 3; fr[26] = 1;                         /* (algorithm 3, sequence 1, status 0) */
    fr[30] = 19;
    memcpy(fr + 32, a.scalar, 32);
    memcpy(fr + 64, a.element, 64);
    fr[0] = 0xB0;
    assert(SaeParseCommit(fr, 24 + 6 + 2 + 96, &sf) && sf.seq == 1 && !sf.status && sf.group == 19 &&
           !memcmp(sf.scalar, a.scalar, 32) && !memcmp(sf.element, a.element, 64));
    assert(SaeParseCommit(fr + 30, 2 + 96, &sf) && sf.group == 19 && !memcmp(sf.scalar, a.scalar, 32));
    fr[28] = SAE_STATUS_ANTI_CLOGGING;
    memcpy(fr + 32, "TOKEN123", 8);
    assert(SaeParseCommit(fr + 24, 6 + 2 + 8, &sf) && sf.status == SAE_STATUS_ANTI_CLOGGING && sf.token_len == 8 &&
           !memcmp(sf.token, "TOKEN123", 8));
    fr[28] = SAE_STATUS_GROUP_REFUSED;
    assert(SaeParseCommit(fr + 24, 6 + 2, &sf) && sf.status == SAE_STATUS_GROUP_REFUSED);
    UINT8 cf[6 + 2 + 32] = { 3, 0, 2, 0, 0, 0, 1, 0 };
    memcpy(cf + 8, conf, 32);
    assert(SaeParseConfirm(cf, sizeof(cf), &sf) && sf.seq == 2 && sf.send_confirm == 1 && !memcmp(sf.confirm, conf, 32));
    assert(SaeParseConfirm(cf + 6, 34, &sf) && sf.send_confirm == 1);
    printf("parsers ok\n");
    return 0;
}
