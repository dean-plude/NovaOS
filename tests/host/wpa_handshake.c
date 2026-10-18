/* The kernel's WPA2 supplicant (kernel/nt/wpa.c, over Mbed TLS) against
 * wditest.sys's authenticator (tools/drivertest/wpaap.h, its own SHA-1 and
 * AES) on the host: known-answer vectors for both, then the 4-way
 * handshake, a group rekey, a wrong passphrase and a tampered message 3.
 * It prints the derived keys for tests/tools/test_wpa_handshake.py to
 * check against Python's hashlib. */
#include "../../kernel/nt/wpa.c"
#include "../../tools/drivertest/wpaap.h"
#include <assert.h>
#include <stdio.h>

void TlsEntropyOutput(UINT8 *out, size_t len) { memset(out, 0x5A, len); }   /* (the SNonce: fixed here) */

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

static const UINT8 AP_IE[] = { 48, 20, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 2,
                               0x0c, 0x00 };
static const UINT8 STA_IE[] = { 48, 20, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 2,
                                0, 0 };

static void setup(WpaSupplicant *s, WpaAp *a, const char *pass_sta, const char *pass_ap)
{
    static const UINT8 aa[6] = { 0x02, 0x4e, 0x6f, 0x76, 0x61, 0x01 }, spa[6] = { 0x02, 0x4e, 0x6f, 0x76, 0x61, 0x00 };
    memset(s, 0, sizeof(*s));
    memset(a, 0, sizeof(*a));
    assert(WpaPassphraseKey(pass_sta, "NovaNet", s->pmk));
    assert(WpaPassphraseKey(pass_ap, "NovaNet", a->pmk));
    memcpy(s->aa, aa, 6); memcpy(s->spa, spa, 6); memcpy(a->aa, aa, 6); memcpy(a->spa, spa, 6);
    memcpy(s->own_ie, STA_IE, s->own_ie_len = sizeof(STA_IE));
    memcpy(s->ap_ie, AP_IE, s->ap_ie_len = sizeof(AP_IE));
    a->ap_ie = AP_IE; a->ap_ie_len = sizeof(AP_IE); a->sta_ie = STA_IE; a->sta_ie_len = sizeof(STA_IE);
    for (int i = 0; i < 32; i++) a->anonce[i] = (UINT8)(i * 7 + 1);
    for (int i = 0; i < 16; i++) a->gtk[i] = (UINT8)(0xA0 + i);
    a->gtk_id = 1;
    a->rsc[0] = 5;
    WpaStart(s);
}

int main(void)
{
    /* known answers: SHA-1 ("abc"), the 802.11 PRF (J.3.2 test 1), AES-128 (FIPS 197 C.1), key wrap (RFC 3394 4.1) */
    UINT8 d[64], k[16], in[32], out[40], want[64];
    Sha1 c;
    sha1_init(&c); sha1_update(&c, (const u8 *)"abc", 3); sha1_final(&c, d);
    unhex("a9993e364706816aba3e25717850c26c9cd0d89d", want);
    assert(!memcmp(d, want, 20));
    UINT8 key[20];
    memset(key, 0x0b, 20);
    UINT8 prf_in[64], r[80];
    memcpy(prf_in, "prefix", 7);
    memcpy(prf_in + 7, "Hi There", 8);
    for (UINT8 i = 0; i < 4; i++) { prf_in[15] = i; ap_hmac(key, 20, prf_in, 16, 0, 0, r + 20 * i); }
    unhex("bcd4c650b30b9684951829e0d75f9d54b862175ed9f00606e17d8da35402ffee75df78c3d31e0f889f012120c0862beb67753e7439ae242edb8373698356cf5a", want);
    assert(!memcmp(r, want, 64));
    unhex("000102030405060708090a0b0c0d0e0f", k);
    unhex("00112233445566778899aabbccddeeff", in);
    aes128_encrypt(k, in, out);
    unhex("69c4e0d86a7b0430d8cdb78070b4c55a", want);
    assert(!memcmp(out, want, 16));
    aes_wrap(k, in, 16, out);
    unhex("1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5", want);
    assert(!memcmp(out, want, 24));
    UINT8 pmk[32];
    assert(WpaPassphraseKey("password", "IEEE", pmk));
    hex("psk_ieee", pmk, 32);
    assert(!WpaPassphraseKey("short", "IEEE", pmk));
    assert(WpaPassphraseKey("3568811af581191042f36de61f6f46cb29a40196b48041ee91df3777b15227c4", "NovaNet", pmk));
    hex("psk_hex", pmk, 32);

    /* the 4-way handshake, then a group rekey */
    static WpaSupplicant s;
    static WpaAp a;
    UINT8 f[512], rep[WPA_FRAME_MAX];
    UINT32 n, rl;
    setup(&s, &a, "nova-wpa2-passphrase", "nova-wpa2-passphrase");
    hex("pmk", s.pmk, 32);
    n = ap_msg1(&a, f);
    assert(WpaReceive(&s, f, n, rep, &rl) == WPA_REPLY);
    assert(ap_msg2(&a, rep, rl));
    hex("ptk", a.ptk, 48);
    n = ap_msg3(&a, f);
    assert(WpaReceive(&s, f, n, rep, &rl) == WPA_PAIRWISE);
    assert(ap_msg4(&a, rep, rl));
    assert(!memcmp(WpaTk(&s), AP_TK(&a), 16) && s.gtk_len == 16 && !memcmp(s.gtk, a.gtk, 16) && s.gtk_id == 1 &&
           s.gtk_rsc[0] == 5);
    assert(WpaReceive(&s, f, n, rep, &rl) == WPA_DROP);                  /* (message 3 again: replayed) */
    for (int i = 0; i < 16; i++) a.gtk[i] = (UINT8)(0xB0 + i);
    a.gtk_id = 2;
    n = ap_group1(&a, f);
    assert(WpaReceive(&s, f, n, rep, &rl) == WPA_GROUP);
    assert(ap_group2(&a, rep, rl));
    assert(s.gtk_id == 2 && !memcmp(s.gtk, a.gtk, 16));
    printf("handshake ok\n");

    /* a wrong passphrase: the access point refuses message 2 */
    setup(&s, &a, "not-the-passphrase", "nova-wpa2-passphrase");
    n = ap_msg1(&a, f);
    assert(WpaReceive(&s, f, n, rep, &rl) == WPA_REPLY);
    assert(!ap_msg2(&a, rep, rl));
    printf("wrong key refused: %s\n", a.why);

    /* a message 3 whose RSN element is not the beacon's (a downgrade), and one with a broken MIC */
    setup(&s, &a, "nova-wpa2-passphrase", "nova-wpa2-passphrase");
    n = ap_msg1(&a, f);
    WpaReceive(&s, f, n, rep, &rl);
    assert(ap_msg2(&a, rep, rl));
    static const UINT8 other[] = { 48, 20, 1, 0, 0x00, 0x0f, 0xac, 2, 1, 0, 0x00, 0x0f, 0xac, 2, 1, 0, 0x00, 0x0f, 0xac, 2,
                                   0x0c, 0x00 };
    a.ap_ie = other;
    n = ap_msg3(&a, f);
    assert(WpaReceive(&s, f, n, rep, &rl) == WPA_DROP);
    printf("downgrade refused: %s\n", s.why);
    a.ap_ie = AP_IE;
    n = ap_msg3(&a, f);
    f[n - 1] ^= 1;
    assert(WpaReceive(&s, f, n, rep, &rl) == WPA_DROP);
    printf("tampered refused: %s\n", s.why);
    return 0;
}
