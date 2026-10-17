/*
 * net.h — NovaOS networking: interface setup and asynchronous operations
 *
 * lwIP (third_party/lwip) runs on the "net" kernel thread.  Apps start
 * operations (DNS lookup, ping, HTTP GET), then poll op->state from their
 * UI tick.  Operations come from a fixed pool owned by this module: an
 * app calls NetRelease() when done (even if the operation is still
 * running); the slot is recycled only once lwIP can no longer touch it.
 */

#pragma once

#include "../include/types.h"

/* Bring up the NIC, lwIP and DHCP, and start the net thread. */
bool NetInitialize(void);
bool NetAvailable(void);
void NetResume(void);                     /* after S3: the adapter */

/* A network adapter another driver runs: a Windows miniport through
 * NovaOS's NDIS host (kernel/nt/ndis.c).  NetAttach makes it the adapter
 * the stack uses (DHCP starts again on it); NetDetach gives it up, leaving
 * loopback only until another attaches. */
typedef struct {
    const char *(*name)(void);
    void        (*mac)(UINT8 mac[6]);
    bool        (*link_up)(void);
    bool        (*transmit)(const void *frame, UINT16 len);   /* false: no room now */
    int         (*receive)(void *buf, int cap);               /* a frame's length, or 0 */
} NetAdapter;
void NetAttach(const NetAdapter *a);
void NetDetach(const NetAdapter *a);

/* An IPv4 or IPv6 address: IPv4 in a[0..3], both in network byte order */
typedef struct {
    bool   v6;
    UINT8  a[16];
} NetIp;

#define NET_IP6_MAX 4

typedef struct {
    bool   present;          /* a supported NIC was found */
    bool   link;             /* cable/link up */
    bool   configured;       /* DHCP lease obtained (or a manual address, with a link) */
    bool   manual_ip;        /* the IPv4 address was set by hand (no DHCP) */
    bool   manual_dns;       /* ... and the DNS servers */
    UINT8  mac[6];
    UINT32 ip, mask, gw;     /* network byte order (as lwIP stores them) */
    UINT32 dns[2];
    const char *adapter;
    int    ip6_count;        /* valid IPv6 addresses (link-local first) */
    UINT8  ip6[NET_IP6_MAX][16];
    bool   ip6_preferred[NET_IP6_MAX];   /* else still tentative or deprecated */
    bool   ip6_dhcp[NET_IP6_MAX];        /* leased from a DHCPv6 server (else SLAAC, or link-local) */
    UINT32 ip6_valid[NET_IP6_MAX], ip6_pref[NET_IP6_MAX];   /* lifetimes left, s (0xFFFFFFFF: infinite) */
    UINT32 if_index;         /* the interface's number (IPv6 zone) */
    NetIp  dns_all[3];       /* every DNS server, IPv4 and IPv6 (v6 false, zero: none) */
    UINT64 in_octets, out_octets, in_frames, out_frames;   /* what the adapter moved since boot */
    /* stateful DHCPv6 (RFC 8415): the client's DUID and IAID, and while
     * an address is leased (dhcp6) the server, T1, T2 and the valid
     * lifetime as granted, the lease's age, and the domain search list */
    bool   dhcp6;
    UINT8  dhcp6_server[16];
    UINT32 dhcp6_iaid;
    int    dhcp6_duid_len;
    UINT8  dhcp6_duid[20];
    UINT32 dhcp6_t1, dhcp6_t2, dhcp6_valid, dhcp6_age;   /* s (0xFFFFFFFF: infinite) */
    char   dns_suffix[128];  /* "a.example,b.example" (empty: none) */
} NetStatus;

void NetGetStatus(NetStatus *st);
void NetFormatIp(UINT32 ip_be, char *buf, int cap);        /* "10.0.2.15" */
bool NetParseIp(const char *s, UINT32 *ip_be);
/* Either family: "10.0.2.15", "fd00::15" (cap 46 holds any) */
void NetFormatAddr(const NetIp *ip, char *buf, int cap);
bool NetParseAddr(const char *s, NetIp *ip);
/* Whether the interface has an IPv6 address other than its link-local one */
bool NetHasIp6(void);

/* Stateful DHCPv6: ipconfig /release6 (1: released and the server
 * confirmed, 0: released without an answer, -1: no lease) and /renew6
 * (true once a server has granted or extended the lease, up to 10 s);
 * NetShutdown releases it before the machine powers off or restarts. */
int  NetDhcp6Release(void);
bool NetDhcp6Renew(void);
void NetShutdown(void);

/* Manual IPv4 settings (Settings > Network's Edit, netsh).  All network
 * byte order; manual_ip off: the address comes from DHCP; manual_dns off:
 * the DNS servers do too.  NetSetConfig applies them at once and keeps
 * them in the registry (false with @err set when they are not valid);
 * NetRestoreConfig puts the saved ones back once the registry is loaded. */
typedef struct {
    bool   manual_ip;
    UINT32 ip, mask, gw;     /* (gw 0: none) */
    bool   manual_dns;
    UINT32 dns[2];           /* preferred, alternate (0: none) */
} NetConfig;
void NetGetConfig(NetConfig *c);
bool NetSetConfig(const NetConfig *c, char *err, int cap);
void NetRestoreConfig(void);

/* -----------------------------------------------------------------------
 * Asynchronous operations
 * ----------------------------------------------------------------------- */
typedef enum { NET_PENDING = 0, NET_DONE = 1, NET_FAILED = -1 } NetState;
typedef enum { NETOP_DNS, NETOP_PING, NETOP_HTTP } NetOpKind;

#define NET_HTTP_MAX  (1024u * 1024 * 1024) /* largest response kept (App Store installers: LibreOffice 350 MB) */
/* The longest URL path (and Location header) followed: download sites
 * redirect to signed URLs (GitHub's release assets: ~900 characters) */
#define NET_URL_MAX   2048

typedef struct NetOp {
    NetOpKind       kind;
    volatile int    state;          /* NetState */
    char            error[64];      /* set when NET_FAILED */

    UINT32          ip;             /* DNS result / ping or HTTP target (IPv4) */
    NetIp           addr;           /* the same, IPv4 or IPv6 */

    /* ping */
    UINT16          seq;
    int             rtt_ms;
    int             ttl;

    /* HTTP: the response (status line + headers + body; a chunked body
     * is delivered already decoded) */
    char           *data;
    UINT32          len;
    char            tls_info[64];   /* https: "TLS 1.3, TLS_AES_128_GCM_SHA256" */
    bool            reused;         /* sent on a kept-alive connection */
    bool            resumed;        /* TLS session resumed (abbreviated handshake) */

    /* private */
    bool            in_use, released, retried;
    UINT64          started, deadline;   /* ticks */
    void           *conn;           /* http.c connection while in flight */
    UINT32          cap;
    char            request[NET_URL_MAX + 512];
} NetOp;

/* Resolve a host name (or address literal) to an address in op->addr
 * (op->ip too when it is IPv4): @family 4 or 6 for only that family, 0
 * for either (IPv6 first when the interface has a global IPv6 address) */
NetOp *NetResolveEx(const char *host, int family);
NetOp *NetResolve(const char *host);                 /* (family 0) */
/* Send one ICMP (or ICMPv6) echo request; completes on the reply or after 2 s. */
NetOp *NetPing(UINT32 ip_be, UINT16 seq);
NetOp *NetPingAddr(const NetIp *ip, UINT16 seq);
/* HTTP/1.1 GET http[s]://host:port/path.  Connections are kept alive and
 * reused for later requests to the same server; with @https the
 * connection uses TLS 1.2/1.3, the server's certificate must chain to a
 * trusted root and match @host, and sessions are resumed when possible. */
NetOp *NetHttpGet(UINT32 ip_be, UINT16 port, const char *host, const char *path,
                  bool https);
NetOp *NetHttpGetAddr(const NetIp *ip, UINT16 port, const char *host, const char *path,
                      bool https);
/* Done with an operation (safe while it is still pending). */
void   NetRelease(NetOp *op);

/* Split "http://host[:port]/path" (scheme optional).  *https is set for
 * https:// URLs (default port 443).  Returns false if malformed. */
bool   NetParseUrl(const char *url, char *host, int host_cap, UINT16 *port,
                   char *path, int path_cap, bool *https);

/* Parse a completed HTTP response. Returns the status code (0 if
 * malformed); body/body_len point into op->data; location (may be NULL)
 * receives a Location header if present. */
int    NetHttpParse(const NetOp *op, const char **body, UINT32 *body_len,
                    char *location, int loc_cap);

/* -----------------------------------------------------------------------
 * Trusted root certificates (HTTPS)
 * ----------------------------------------------------------------------- */
/* Number of trusted roots; *imported (may be NULL) gets the user-added count. */
int    NetRootCount(int *imported);
/* Name of root i (its common name); *imported set for user-added roots. */
bool   NetRootName(int i, char *buf, int cap, bool *imported);
/* Trust the CA certificate(s) in a PEM or DER file.  Returns the number
 * added; 0 with *err set on failure.  Lasts until reboot. */
int    NetImportRoots(const void *data, UINT32 len, char *err, int err_cap);
