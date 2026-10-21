/*
 * firewall.c — the firewall: Windows Defender Firewall's profiles and
 * rules, and the network list that says which profile is in force
 *
 * Profiles: Domain, Private and Public, each on or off with its default
 * actions (inbound blocked, outbound allowed) and "block all incoming
 * connections".  The one in force is the category of the network the
 * adapter is on; a network seen for the first time is Public, and the
 * desktop asks once whether this PC should be discoverable on it (Yes
 * makes it Private).  Loopback traffic is never filtered by the firewall.
 *
 * Rules: allow or block, inbound or outbound, for a program, protocol,
 * local and remote ports and addresses, on some profiles.  Inbound, a
 * block rule wins over an allow rule, an allow rule over the profile's
 * default; outbound the same.  The firewall decides at the ALE layers
 * (a connection or a first datagram, either way), as Windows' does; the
 * replies of an allowed flow pass on their own (sock.c keeps the flows).
 *
 * Where it is kept (as Windows keeps it):
 *   HKLM\SYSTEM\CurrentControlSet\Services\SharedAccess\Parameters\FirewallPolicy
 *       {DomainProfile,StandardProfile,PublicProfile}  EnableFirewall,
 *           DefaultInboundAction, DefaultOutboundAction (1 block, 0 allow),
 *           DoNotAllowExceptions, DisableNotifications, DisableUnicastResponsesToMulticastBroadcast
 *       FirewallRules  one REG_SZ per rule, named by its id:
 *           "v2.31|Action=Allow|Active=TRUE|Dir=In|Protocol=6|Profile=Public|LPort=80|App=...|Name=...|"
 *   HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\NetworkList
 *       Profiles\{GUID}  ProfileName, Description, Category (0 public, 1
 *           private, 2 domain), NameType (6 wired), Managed
 *       Signatures\Unmanaged\SIGNATURE  ProfileGuid, DefaultGatewayMac,
 *           Description, FirstNetwork (the signature: the gateway's MAC)
 *   HKLM\SYSTEM\CurrentControlSet\Control\Network\NewNetworkWindowOff:
 *       when the key exists, new networks do not ask (Windows' switch)
 *
 * Not here yet: IPsec and connection security rules, the "allow this
 * app?" notification when a blocked program listens, ICMP rules (ICMP is
 * answered by the stack itself), service-restricted rules, interface-type
 * conditions, and Group Policy's separate store.
 */

#include "wfp.h"
#include "net.h"
#include "net_internal.h"
#include "../ke/printf.h"
#include "../ke/scheduler.h"
#include "../mm/vmm.h"
#include "../lib/string.h"
#include "../um/um.h"

#define POLICY_KEY  "Machine\\SYSTEM\\CurrentControlSet\\Services\\SharedAccess\\Parameters\\FirewallPolicy"
#define RULES_KEY   POLICY_KEY "\\FirewallRules"
#define NETLIST_KEY "Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\NetworkList"
#define NO_PROMPT_KEY "Machine\\SYSTEM\\CurrentControlSet\\Control\\Network\\NewNetworkWindowOff"

#define MAX_RULES  512
#define MAX_PORTS  16
#define MAX_ADDRS  8

typedef struct { UINT16 lo, hi; } PortRange;
typedef struct {
    bool  v6;
    int   kw;                       /* 0 a range; 1 LocalSubnet; 2 DefaultGateway */
    UINT8 lo[16], hi[16];           /* network order, inclusive */
} AddrRange;
typedef struct {
    int       n;                    /* -1 any */
    bool      bad;                  /* a keyword NovaOS cannot match (RPC, IPHTTPS...): matches nothing */
    PortRange r[MAX_PORTS];
} PortSet;
typedef struct {
    int       n;                    /* -1 any */
    bool      bad;
    AddrRange r[MAX_ADDRS];
} AddrSet;

typedef struct {
    NovaFwRule r;
    char      app[NOVAWFP_APP_MAX]; /* (WfpAppNormalize'd; "" any) */
    PortSet   lp, rp;
    AddrSet   la, ra;
} Rule;

static const char *const g_prof_keys[3] = { "DomainProfile", "StandardProfile", "PublicProfile" };
static NovaFwProfile g_prof[3];
static Rule   *g_rules[MAX_RULES];
static bool    g_ready;
static NovaFwNetwork g_net;           /* the network the adapter is on */
static char    g_sig[40];             /* ... its signature ("" none) */
static UINT64  g_next_poll;
/* The adapter's addresses, for LocalSubnet and DefaultGateway (FwPoll copies them) */
static UINT32  g_if_ip, g_if_mask, g_if_gw;
static int     g_if_n6;
static UINT8   g_if_ip6[NET_IP6_MAX][16];

static int prof_index(UINT32 bit) { return bit == NOVAFW_DOMAIN ? 0 : bit == NOVAFW_PRIVATE ? 1 : 2; }

UINT32 FwCurrentProfiles(void)
{
    if (!g_net.connected) return NOVAFW_PUBLIC;
    return g_net.category == 2 ? NOVAFW_DOMAIN : g_net.category == 1 ? NOVAFW_PRIVATE : NOVAFW_PUBLIC;
}

/* ---------------------------------------------------------------------------
 * Parsing a rule's ports and addresses
 * ------------------------------------------------------------------------- */
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static bool eq_ci(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) if (lower(*a) != lower(*b)) return false;
    return !*a && !*b;
}

/* The next comma-separated item of *s into @out, trimmed; false at the end */
static bool next_item(const char **s, char *out, int cap)
{
    const char *p = *s;
    while (*p == ' ' || *p == ',') p++;
    if (!*p) return false;
    int n = 0;
    while (*p && *p != ',') { if (n < cap - 1) out[n++] = *p; p++; }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = 0;
    *s = p;
    return true;
}

static bool any_text(const char *s) { return !s[0] || eq_ci(s, "*") || eq_ci(s, "any"); }

static bool parse_num(const char *s, UINT32 *v)
{
    UINT32 n = 0;
    if (!*s) return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return false;
        n = n * 10 + (UINT32)(*s - '0');
        if (n > 0xFFFF) return false;
    }
    *v = n;
    return true;
}

/* "80,443,8000-8080" ("" or "*": any).  False: malformed */
static bool parse_ports(const char *text, PortSet *ps)
{
    memset(ps, 0, sizeof(*ps));
    if (any_text(text)) { ps->n = -1; return true; }
    char item[32];
    const char *s = text;
    while (next_item(&s, item, sizeof(item))) {
        char *dash = strchr(item, '-');
        UINT32 lo, hi;
        if (dash) {
            *dash = 0;
            if (!parse_num(item, &lo) || !parse_num(dash + 1, &hi) || lo > hi) return false;
        } else if (parse_num(item, &lo)) hi = lo;
        else {                                         /* RPC, RPC-EPMap, IPHTTPS, Teredo, Ply2Disc, mDNS... */
            ps->bad = true;
            continue;
        }
        if (ps->n == MAX_PORTS) return false;
        ps->r[ps->n].lo = (UINT16)lo;
        ps->r[ps->n].hi = (UINT16)hi;
        ps->n++;
    }
    return true;
}

static void prefix_range(const UINT8 *a, int bits, int len, UINT8 *lo, UINT8 *hi)
{
    for (int i = 0; i < len; i++, bits -= 8) {
        UINT8 m = bits >= 8 ? 0xFF : bits <= 0 ? 0 : (UINT8)(0xFF << (8 - bits));
        lo[i] = a[i] & m;
        hi[i] = (UINT8)(a[i] | (UINT8)~m);
    }
}

static int mask_bits(const UINT8 *m)
{
    int bits = 0;
    for (int i = 0; i < 4; i++) for (int b = 7; b >= 0; b--) { if (!(m[i] & (1 << b))) return bits; bits++; }
    return bits;
}

/* "10.0.0.0/255.0.0.0", "192.168.1.0/24", "a-b", "fd00::/8", "LocalSubnet" */
static bool parse_addrs(const char *text, AddrSet *as)
{
    memset(as, 0, sizeof(*as));
    if (any_text(text)) { as->n = -1; return true; }
    char item[96];
    const char *s = text;
    while (next_item(&s, item, sizeof(item))) {
        if (as->n == MAX_ADDRS) return false;
        AddrRange *r = &as->r[as->n];
        memset(r, 0, sizeof(*r));
        if (eq_ci(item, "LocalSubnet")) { r->kw = 1; as->n++; continue; }
        if (eq_ci(item, "DefaultGateway")) { r->kw = 2; as->n++; continue; }
        char *slash = strchr(item, '/'), *dash = strchr(item, '-');
        if (slash) *slash = 0;
        if (dash && !slash) *dash = 0;
        NetIp a;
        if (!NetParseAddr(item, &a)) {                  /* DNS, DHCP, WINS, Intranet, PlayToDevice... */
            if (strchr(item, '.') || strchr(item, ':')) return false;
            as->bad = true;
            continue;
        }
        r->v6 = a.v6;
        int len = a.v6 ? 16 : 4;
        if (slash) {
            NetIp m;
            UINT32 bits;
            if (!a.v6 && NetParseAddr(slash + 1, &m) && !m.v6) bits = (UINT32)mask_bits(m.a);
            else if (!parse_num(slash + 1, &bits) || bits > (UINT32)(len * 8)) return false;
            prefix_range(a.a, (int)bits, len, r->lo, r->hi);
        } else if (dash) {
            NetIp b;
            if (!NetParseAddr(dash + 1, &b) || b.v6 != a.v6) return false;
            memcpy(r->lo, a.a, (size_t)len);
            memcpy(r->hi, b.a, (size_t)len);
        } else {
            memcpy(r->lo, a.a, (size_t)len);
            memcpy(r->hi, a.a, (size_t)len);
        }
        as->n++;
    }
    return true;
}

static bool addr_in(const AddrSet *as, bool v6, const UINT8 *a)
{
    if (as->n < 0) return true;
    int len = v6 ? 16 : 4;
    for (int i = 0; i < as->n; i++) {
        const AddrRange *r = &as->r[i];
        if (r->kw == 1) {                                  /* LocalSubnet: on the adapter's own network */
            if (!v6 && g_if_mask && ((*(const UINT32 *)a ^ g_if_ip) & g_if_mask) == 0) return true;
            if (v6) for (int k = 0; k < g_if_n6; k++) if (!memcmp(g_if_ip6[k], a, 8)) return true;
            continue;
        }
        if (r->kw == 2) {
            if (!v6 && g_if_gw && *(const UINT32 *)a == g_if_gw) return true;
            continue;
        }
        if (r->v6 != v6) continue;
        if (memcmp(a, r->lo, (size_t)len) >= 0 && memcmp(a, r->hi, (size_t)len) <= 0) return true;
    }
    return false;
}

static bool port_in(const PortSet *ps, UINT16 p)
{
    if (ps->n < 0) return true;
    for (int i = 0; i < ps->n; i++) if (p >= ps->r[i].lo && p <= ps->r[i].hi) return true;
    return false;
}

/* Checks a rule and makes its parsed form; false (with @err) if it is not valid */
static bool compile(const NovaFwRule *in, Rule *out, const char **err)
{
    memset(out, 0, sizeof(*out));
    out->r = *in;
    NovaFwRule *r = &out->r;
    r->name[sizeof(r->name) - 1] = r->desc[sizeof(r->desc) - 1] = r->group[sizeof(r->group) - 1] = 0;
    r->app[sizeof(r->app) - 1] = r->service[sizeof(r->service) - 1] = 0;
    r->lports[sizeof(r->lports) - 1] = r->rports[sizeof(r->rports) - 1] = 0;
    r->laddrs[sizeof(r->laddrs) - 1] = r->raddrs[sizeof(r->raddrs) - 1] = 0;
    if (!r->name[0]) { *err = "a rule needs a name"; return false; }
    if (r->dir != 1 && r->dir != 2) { *err = "dir must be in or out"; return false; }
    if (r->action != 0 && r->action != 1) { *err = "action must be allow or block"; return false; }
    if (r->protocol < 0 || r->protocol > 256) { *err = "the protocol must be 0-255 or any"; return false; }
    if (!r->profiles) r->profiles = NOVAFW_ALL;
    bool ports_ok = r->protocol == 6 || r->protocol == 17;
    if ((!any_text(r->lports) || !any_text(r->rports)) && !ports_ok) {
        *err = "ports need protocol TCP or UDP";
        return false;
    }
    if (!parse_ports(r->lports, &out->lp) || !parse_ports(r->rports, &out->rp)) { *err = "a port is not valid"; return false; }
    if (!parse_addrs(r->laddrs, &out->la) || !parse_addrs(r->raddrs, &out->ra)) { *err = "an address is not valid"; return false; }
    if (!any_text(r->app)) WfpAppNormalize(r->app, out->app, sizeof(out->app));
    return true;
}

static bool rule_match(const Rule *k, const WfpValues *v)
{
    const NovaFwRule *r = &k->r;
    if (r->protocol != 256 && r->protocol != v->protocol) return false;
    if (k->lp.bad && k->lp.n == 0) return false;
    if (k->rp.bad && k->rp.n == 0) return false;
    if (!port_in(&k->lp, v->lport) || !port_in(&k->rp, v->rport)) return false;
    if ((k->la.bad && k->la.n == 0) || (k->ra.bad && k->ra.n == 0)) return false;
    if (!addr_in(&k->la, v->v6, v->local) || !addr_in(&k->ra, v->v6, v->remote)) return false;
    if (k->app[0] && (!v->app || strcmp(k->app, v->app))) return false;
    if (!any_text(r->service)) return false;              /* (a service's own rule: NovaOS cannot tell services' sockets apart) */
    return true;
}

int FwDecide(const WfpValues *v)
{
    if (!g_ready || (v->flags & NOVAWFP_FLAG_IS_LOOPBACK)) return -1;
    bool in;
    if (v->layer == NOVAWFP_L_CONNECT_V4 || v->layer == NOVAWFP_L_CONNECT_V6) in = false;
    else if (v->layer == NOVAWFP_L_RECV_ACCEPT_V4 || v->layer == NOVAWFP_L_RECV_ACCEPT_V6) in = true;
    else return -1;
    const NovaFwProfile *p = &g_prof[prof_index(FwCurrentProfiles())];
    if (!p->enabled) return -1;
    if (in && p->block_all) return 0;
    UINT32 bit = FwCurrentProfiles();
    bool allow = false;
    for (int i = 0; i < MAX_RULES; i++) {
        const Rule *k = g_rules[i];
        if (!k || !k->r.enabled || k->r.dir != (in ? 1 : 2) || !(k->r.profiles & bit)) continue;
        if (!rule_match(k, v)) continue;
        if (k->r.action == 0) return 0;
        allow = true;
    }
    if (allow) return 1;
    return in ? (p->default_in ? 1 : 0) : (p->default_out ? 1 : 0);
}

/* ---------------------------------------------------------------------------
 * The registry: rules in Windows' text form
 * ------------------------------------------------------------------------- */
static void add_field(char *s, int cap, const char *key, const char *val)
{
    int n = (int)strlen(s);
    if (n + (int)strlen(key) + (int)strlen(val) + 3 >= cap) return;
    strcat(s, key);
    strcat(s, "=");
    n = (int)strlen(s);
    for (; *val && n < cap - 2; val++) s[n++] = *val == '|' ? '/' : *val;
    s[n++] = '|';
    s[n] = 0;
}

/* Each comma-separated item as its own field (LPort=80|LPort=443|) */
static void add_list(char *s, int cap, const char *key, const char *list)
{
    if (any_text(list)) return;
    char item[96];
    const char *p = list;
    while (next_item(&p, item, sizeof(item))) add_field(s, cap, key, item);
}

static void rule_text(const NovaFwRule *r, char *s, int cap)
{
    char num[16];
    strcpy(s, "v2.31|");
    add_field(s, cap, "Action", r->action ? "Allow" : "Block");
    add_field(s, cap, "Active", r->enabled ? "TRUE" : "FALSE");
    add_field(s, cap, "Dir", r->dir == 1 ? "In" : "Out");
    if (r->protocol != 256) { ksnprintf(num, sizeof(num), "%d", r->protocol); add_field(s, cap, "Protocol", num); }
    if ((r->profiles & 7) != 7) {
        if (r->profiles & NOVAFW_DOMAIN)  add_field(s, cap, "Profile", "Domain");
        if (r->profiles & NOVAFW_PRIVATE) add_field(s, cap, "Profile", "Private");
        if (r->profiles & NOVAFW_PUBLIC)  add_field(s, cap, "Profile", "Public");
    }
    add_list(s, cap, "LPort", r->lports);
    add_list(s, cap, "RPort", r->rports);
    /* addresses: IPv4 ones as LA4/RA4, IPv6 ones as LA6/RA6 (keywords: both) */
    for (int side = 0; side < 2; side++) {
        const char *list = side ? r->raddrs : r->laddrs;
        if (any_text(list)) continue;
        char item[96];
        const char *p = list;
        while (next_item(&p, item, sizeof(item))) {
            bool six = strchr(item, ':') != NULL, kw = !strchr(item, '.') && !six;
            if (kw || !six) add_field(s, cap, side ? "RA4" : "LA4", item);
            if (kw || six)  add_field(s, cap, side ? "RA6" : "LA6", item);
        }
    }
    if (r->app[0] && !any_text(r->app)) add_field(s, cap, "App", r->app);
    if (r->service[0] && !any_text(r->service)) add_field(s, cap, "Svc", r->service);
    add_field(s, cap, "Name", r->name);
    if (r->desc[0]) add_field(s, cap, "Desc", r->desc);
    if (r->group[0]) add_field(s, cap, "EmbedCtxt", r->group);
    if (r->edge) add_field(s, cap, "Edge", "TRUE");
}

static void append_list(char *list, int cap, const char *item)
{
    if (list[0] && strlen(list) + 1 < (size_t)cap) strcat(list, ",");
    if (strlen(list) + strlen(item) < (size_t)cap) strcat(list, item);
}

/* A rule from its text; false if it is not one this firewall reads */
static bool rule_parse(const char *s, NovaFwRule *r)
{
    memset(r, 0, sizeof(*r));
    r->enabled = 1;
    r->protocol = 256;
    if (strncmp(s, "v2.", 3)) return false;
    s = strchr(s, '|');
    char f[300], la4[NOVAFW_TEXT] = "", la6[NOVAFW_TEXT] = "", ra4[NOVAFW_TEXT] = "", ra6[NOVAFW_TEXT] = "";
    while (s && *s == '|') {
        s++;
        int n = 0;
        while (*s && *s != '|') { if (n < (int)sizeof(f) - 1) f[n++] = *s; s++; }
        f[n] = 0;
        char *eq = strchr(f, '=');
        if (!eq) continue;
        *eq = 0;
        const char *v = eq + 1;
        if (eq_ci(f, "Action")) r->action = eq_ci(v, "Allow") ? 1 : 0;
        else if (eq_ci(f, "Active")) r->enabled = eq_ci(v, "TRUE");
        else if (eq_ci(f, "Dir")) r->dir = eq_ci(v, "In") ? 1 : 2;
        else if (eq_ci(f, "Protocol")) { UINT32 p; r->protocol = parse_num(v, &p) && p < 256 ? (int)p : 256; }
        else if (eq_ci(f, "Profile")) r->profiles |= eq_ci(v, "Domain") ? NOVAFW_DOMAIN : eq_ci(v, "Private") ? NOVAFW_PRIVATE : NOVAFW_PUBLIC;
        else if (eq_ci(f, "LPort") || eq_ci(f, "LPort2_10")) append_list(r->lports, sizeof(r->lports), v);
        else if (eq_ci(f, "RPort") || eq_ci(f, "RPort2_10")) append_list(r->rports, sizeof(r->rports), v);
        else if (eq_ci(f, "LA4")) append_list(la4, sizeof(la4), v);
        else if (eq_ci(f, "LA6")) append_list(la6, sizeof(la6), v);
        else if (eq_ci(f, "RA4")) append_list(ra4, sizeof(ra4), v);
        else if (eq_ci(f, "RA6")) append_list(ra6, sizeof(ra6), v);
        else if (eq_ci(f, "App")) strncpy(r->app, v, sizeof(r->app) - 1);
        else if (eq_ci(f, "Svc")) strncpy(r->service, v, sizeof(r->service) - 1);
        else if (eq_ci(f, "Name")) strncpy(r->name, v, sizeof(r->name) - 1);
        else if (eq_ci(f, "Desc")) strncpy(r->desc, v, sizeof(r->desc) - 1);
        else if (eq_ci(f, "EmbedCtxt")) strncpy(r->group, v, sizeof(r->group) - 1);
        else if (eq_ci(f, "Edge")) r->edge = eq_ci(v, "TRUE");
    }
    /* a keyword appears in both families' lists: keep it once */
    for (int side = 0; side < 2; side++) {
        char *dst = side ? r->raddrs : r->laddrs;
        const char *four = side ? ra4 : la4, *six = side ? ra6 : la6;
        strncpy(dst, four, NOVAFW_TEXT - 1);
        char item[96];
        const char *p = six;
        while (next_item(&p, item, sizeof(item)))
            if (strchr(item, ':')) append_list(dst, NOVAFW_TEXT, item);
            else if (!four[0]) append_list(dst, NOVAFW_TEXT, item);
    }
    if (!r->profiles) r->profiles = NOVAFW_ALL;
    return r->dir != 0 && r->name[0];
}

static void ascii_of(const UINT16 *w, int n, char *out, int cap)
{
    int i = 0;
    for (; i < n && i < cap - 1 && w[i]; i++) out[i] = w[i] < 0x80 ? (char)w[i] : '?';
    out[i] = 0;
}

static void save_rule(const NovaFwRule *r)
{
    char *s = kmalloc(1400);
    if (!s) return;
    rule_text(r, s, 1400);
    um_registry_set_sz(RULES_KEY, r->id, s);
    kfree(s);
}

static void save_profile(int i)
{
    char path[160];
    ksnprintf(path, sizeof(path), POLICY_KEY "\\%s", g_prof_keys[i]);
    const NovaFwProfile *p = &g_prof[i];
    um_registry_set_dword(path, "EnableFirewall", p->enabled ? 1 : 0);
    um_registry_set_dword(path, "DefaultInboundAction", p->default_in ? 0 : 1);
    um_registry_set_dword(path, "DefaultOutboundAction", p->default_out ? 0 : 1);
    um_registry_set_dword(path, "DoNotAllowExceptions", p->block_all ? 1 : 0);
    um_registry_set_dword(path, "DisableNotifications", p->notify_off ? 1 : 0);
    um_registry_set_dword(path, "DisableUnicastResponsesToMulticastBroadcast", p->unicast_off ? 1 : 0);
}

static void profile_defaults(void)
{
    for (int i = 0; i < 3; i++) {
        memset(&g_prof[i], 0, sizeof(g_prof[i]));
        g_prof[i].profile = i == 0 ? NOVAFW_DOMAIN : i == 1 ? NOVAFW_PRIVATE : NOVAFW_PUBLIC;
        g_prof[i].enabled = 1;
        g_prof[i].default_in = 0;
        g_prof[i].default_out = 1;
    }
}

void FwRestore(void)
{
    profile_defaults();
    for (int i = 0; i < 3; i++) {
        char path[160];
        ksnprintf(path, sizeof(path), POLICY_KEY "\\%s", g_prof_keys[i]);
        UINT32 v;
        if (!um_registry_key_exists(path)) { save_profile(i); continue; }
        if (um_registry_get_dword(path, "EnableFirewall", &v)) g_prof[i].enabled = v != 0;
        if (um_registry_get_dword(path, "DefaultInboundAction", &v)) g_prof[i].default_in = v == 0;
        if (um_registry_get_dword(path, "DefaultOutboundAction", &v)) g_prof[i].default_out = v == 0;
        if (um_registry_get_dword(path, "DoNotAllowExceptions", &v)) g_prof[i].block_all = v != 0;
        if (um_registry_get_dword(path, "DisableNotifications", &v)) g_prof[i].notify_off = v != 0;
        if (um_registry_get_dword(path, "DisableUnicastResponsesToMulticastBroadcast", &v)) g_prof[i].unicast_off = v != 0;
    }
    um_registry_create_key(RULES_KEY);
    UINT16 name[64];
    char *text = kmalloc(2048), *data = kmalloc(4096);
    NovaFwRule *r = kmalloc(sizeof(*r));
    int n = 0;
    for (UINT32 i = 0; text && data && r; i++) {
        int nlen; UINT32 type;
        int len = um_registry_enum_value(RULES_KEY, i, name, 64, &nlen, &type, data, 4094);
        if (len < 0) break;
        if (type != 1 || len > 4094) continue;
        ascii_of((const UINT16 *)data, len / 2, text, 2048);
        if (!rule_parse(text, r)) continue;
        ascii_of(name, nlen, r->id, sizeof(r->id));
        Rule *k = kmalloc(sizeof(*k));
        const char *err;
        if (!k || !compile(r, k, &err)) { kfree(k); continue; }
        for (int j = 0; j < MAX_RULES; j++) if (!g_rules[j]) { g_rules[j] = k; k = NULL; n++; break; }
        kfree(k);
    }
    kfree(text); kfree(data); kfree(r);
    g_ready = true;
    kprintf("[FIREWALL] %d rule(s); Domain %s, Private %s, Public %s\n", n,
            g_prof[0].enabled ? "on" : "off", g_prof[1].enabled ? "on" : "off", g_prof[2].enabled ? "on" : "off");
}

/* ---------------------------------------------------------------------------
 * The network list
 * ------------------------------------------------------------------------- */
static void profile_path(const char *guid, char *out, int cap)
{
    ksnprintf(out, (size_t)cap, NETLIST_KEY "\\Profiles\\%s", guid);
}

/* A new network: its profile (Public) and its signature */
static void network_new(const char *sig, const UINT8 mac[6])
{
    char guid[40], path[200], name[64];
    WfpNewKeyText(guid, sizeof(guid));
    int count = 0;
    UINT16 w[64];
    while (um_registry_enum_key(NETLIST_KEY "\\Profiles", (UINT32)count, w, 64, NULL) >= 0) count++;
    if (count) ksnprintf(name, sizeof(name), "Network %d", count + 1);
    else strcpy(name, "Network");
    profile_path(guid, path, sizeof(path));
    um_registry_set_sz(path, "ProfileName", name);
    um_registry_set_sz(path, "Description", name);
    um_registry_set_dword(path, "Managed", 0);
    um_registry_set_dword(path, "Category", 0);
    um_registry_set_dword(path, "NameType", 6);              /* wired */
    um_registry_set_dword(path, "CategoryType", 0);
    ksnprintf(path, sizeof(path), NETLIST_KEY "\\Signatures\\Unmanaged\\%s", sig);
    um_registry_set_sz(path, "ProfileGuid", guid);
    um_registry_set_sz(path, "Description", name);
    um_registry_set_sz(path, "FirstNetwork", name);
    um_registry_set_dword(path, "Source", 8);
    um_registry_set_bin(path, "DefaultGatewayMac", mac, 6);
    kprintf("[FIREWALL] new network \"%s\" %s: Public\n", name, guid);
}

static void network_load(const char *sig)
{
    char path[200], guid[40] = "";
    ksnprintf(path, sizeof(path), NETLIST_KEY "\\Signatures\\Unmanaged\\%s", sig);
    um_registry_get_sz(path, "ProfileGuid", guid, sizeof(guid));
    strcpy(g_net.guid, guid);
    profile_path(guid, path, sizeof(path));
    UINT32 cat = 0;
    um_registry_get_dword(path, "Category", &cat);
    g_net.category = cat <= 2 ? (int)cat : 0;
    if (!um_registry_get_sz(path, "ProfileName", g_net.name, sizeof(g_net.name))) strcpy(g_net.name, "Network");
}

void FwPoll(void)
{
    if (!g_ready || sched_ticks() < g_next_poll) return;
    g_next_poll = sched_ticks() + 100;                         /* (once a second) */
    NetStatus *st = kmalloc(sizeof(*st));
    if (!st) return;
    NetGetStatus(st);
    UINT8 mac[6] = { 0 };
    bool have_mac = st->configured && st->gw && NetGatewayMac(mac);
    net_lock();
    g_if_ip = st->configured ? st->ip : 0;
    g_if_mask = st->configured ? st->mask : 0;
    g_if_gw = st->configured ? st->gw : 0;
    g_if_n6 = st->ip6_count;
    memcpy(g_if_ip6, st->ip6, sizeof(g_if_ip6));
    net_unlock();
    bool up = st->configured && st->gw;
    kfree(st);
    if (!up) {
        g_net.connected = 0;
        g_sig[0] = 0;
        return;
    }
    if (!have_mac) return;                                     /* (ARP has not answered for the gateway yet: an unidentified network, Public) */
    char sig[40];
    ksnprintf(sig, sizeof(sig), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (strcmp(sig, g_sig)) {
        char path[200];
        ksnprintf(path, sizeof(path), NETLIST_KEY "\\Signatures\\Unmanaged\\%s", sig);
        bool fresh = !um_registry_key_exists(path);
        if (fresh) network_new(sig, mac);
        network_load(sig);
        memcpy(g_net.gw_mac, mac, 6);
        strcpy(g_sig, sig);
        g_net.connected = 1;
        g_net.prompt = fresh && !um_registry_key_exists(NO_PROMPT_KEY);
        return;
    }
    /* the category may have changed in the registry (an administrator, a script) */
    char path[200];
    profile_path(g_net.guid, path, sizeof(path));
    UINT32 cat;
    if (um_registry_get_dword(path, "Category", &cat) && cat <= 2 && (int)cat != g_net.category) {
        g_net.category = (int)cat;
        kprintf("[FIREWALL] network \"%s\" is now %s\n", g_net.name, cat == 1 ? "Private" : cat == 2 ? "Domain" : "Public");
    }
}

bool FwGetNetwork(NovaFwNetwork *n) { *n = g_net; return g_net.connected != 0; }
bool FwPromptPending(void) { return g_net.connected && g_net.prompt; }
void FwPromptAnswered(void) { g_net.prompt = 0; }

bool FwSetCategory(int category)
{
    if (!g_net.connected || !g_net.guid[0] || category < 0 || category > 1) return false;
    char path[200];
    profile_path(g_net.guid, path, sizeof(path));
    um_registry_set_dword(path, "Category", (UINT32)category);
    g_net.category = category;
    g_net.prompt = 0;
    kprintf("[FIREWALL] network \"%s\" set to %s\n", g_net.name, category ? "Private" : "Public");
    return true;
}

/* ---------------------------------------------------------------------------
 * NtNovaSockCtl op 23
 * ------------------------------------------------------------------------- */
static int rule_slot(const char *id)
{
    for (int i = 0; i < MAX_RULES; i++) if (g_rules[i] && eq_ci(g_rules[i]->r.id, id)) return i;
    return -1;
}

UINT32 FwCtl(bool admin, UINT32 op, UINT32 profile, void *req, INT64 *result)
{
    *result = 0;
    bool changes = op == NOVAFWC_PROFILE_SET || op == NOVAFWC_RULE_ADD || op == NOVAFWC_RULE_SET ||
                   op == NOVAFWC_RULE_DELETE || op == NOVAFWC_NETWORK_SET || op == NOVAFWC_RESTORE_DEFAULTS;
    if (changes && !admin) return NOVAWFP_E_ACCESS_DENIED;
    switch (op) {
    case NOVAFWC_CURRENT: *result = FwCurrentProfiles(); return 0;
    case NOVAFWC_PROFILE_GET: {
        NovaFwProfile *p = req;
        UINT32 bit = p->profile ? p->profile : profile;
        if (bit != NOVAFW_DOMAIN && bit != NOVAFW_PRIVATE && bit != NOVAFW_PUBLIC) return NOVAWFP_E_INVALID_PARAMETER;
        *p = g_prof[prof_index(bit)];
        return 0;
    }
    case NOVAFWC_PROFILE_SET: {
        NovaFwProfile *p = req;
        if (p->profile != NOVAFW_DOMAIN && p->profile != NOVAFW_PRIVATE && p->profile != NOVAFW_PUBLIC)
            return NOVAWFP_E_INVALID_PARAMETER;
        int i = prof_index(p->profile);
        net_lock();
        g_prof[i] = *p;
        net_unlock();
        save_profile(i);
        kprintf("[FIREWALL] %s profile: %s%s\n", i == 0 ? "Domain" : i == 1 ? "Private" : "Public",
                p->enabled ? "on" : "off", p->block_all ? ", blocking all inbound" : "");
        return 0;
    }
    case NOVAFWC_RULE_COUNT: {
        int n = 0;
        for (int i = 0; i < MAX_RULES; i++) if (g_rules[i]) n++;
        *result = n;
        return 0;
    }
    case NOVAFWC_RULE_AT:
    case NOVAFWC_RULE_GET: {
        NovaFwRule *r = req;
        net_lock();
        int slot = -1;
        if (op == NOVAFWC_RULE_GET) slot = rule_slot(r->id);
        else for (int i = 0, n = 0; i < MAX_RULES && slot < 0; i++) if (g_rules[i] && n++ == r->index) slot = i;
        int idx = r->index;
        if (slot >= 0) { *r = g_rules[slot]->r; r->index = idx; }
        net_unlock();
        return slot >= 0 ? 0 : NOVAWFP_E_NOT_FOUND;
    }
    case NOVAFWC_RULE_ADD:
    case NOVAFWC_RULE_SET: {
        NovaFwRule *r = req;
        if (op == NOVAFWC_RULE_ADD) WfpNewKeyText(r->id, sizeof(r->id));
        r->id[sizeof(r->id) - 1] = 0;
        Rule *k = kmalloc(sizeof(*k));
        if (!k) return NOVAWFP_E_OUTOFMEMORY;
        const char *err = "";
        if (!compile(r, k, &err)) {
            kfree(k);
            strncpy(r->desc, err, sizeof(r->desc) - 1);           /* (why, for netsh) */
            return NOVAWFP_E_INVALID_PARAMETER;
        }
        *r = k->r;
        net_lock();
        int slot = rule_slot(r->id);
        Rule *old = NULL;
        if (op == NOVAFWC_RULE_SET && slot < 0) { net_unlock(); kfree(k); return NOVAWFP_E_NOT_FOUND; }
        if (slot < 0) for (int i = 0; i < MAX_RULES && slot < 0; i++) if (!g_rules[i]) slot = i;
        if (slot < 0) { net_unlock(); kfree(k); return NOVAWFP_E_OUTOFMEMORY; }
        old = g_rules[slot];
        g_rules[slot] = k;
        net_unlock();
        kfree(old);
        save_rule(r);
        return 0;
    }
    case NOVAFWC_RULE_DELETE: {
        NovaFwRule *r = req;
        net_lock();
        int slot = rule_slot(r->id);
        Rule *old = slot >= 0 ? g_rules[slot] : NULL;
        if (slot >= 0) g_rules[slot] = NULL;
        net_unlock();
        if (!old) return NOVAWFP_E_NOT_FOUND;
        um_registry_delete_value(RULES_KEY, old->r.id);
        kfree(old);
        return 0;
    }
    case NOVAFWC_NETWORK_GET: FwGetNetwork(req); return 0;
    case NOVAFWC_NETWORK_SET: {
        NovaFwNetwork *n = req;
        return FwSetCategory(n->category) ? 0 : NOVAWFP_E_NOT_FOUND;
    }
    case NOVAFWC_RESTORE_DEFAULTS: {
        Rule *old[MAX_RULES];
        net_lock();
        for (int i = 0; i < MAX_RULES; i++) { old[i] = g_rules[i]; g_rules[i] = NULL; }
        profile_defaults();
        net_unlock();
        for (int i = 0; i < MAX_RULES; i++) if (old[i]) { um_registry_delete_value(RULES_KEY, old[i]->r.id); kfree(old[i]); }
        for (int i = 0; i < 3; i++) save_profile(i);
        return 0;
    }
    default: return NOVAWFP_E_INVALID_PARAMETER;
    }
}
