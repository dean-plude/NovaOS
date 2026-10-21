/*
 * netsh_fw.c — netsh advfirewall: the firewall from the command line, as
 * installers and scripts drive Windows' (kernel/net/firewall.c)
 *
 *   netsh advfirewall firewall add rule name=N dir=in|out action=allow|block
 *         [program=PATH] [service=S] [description=D] [enable=yes|no]
 *         [profile=public|private|domain|any[,...]] [protocol=tcp|udp|icmpv4|icmpv6|any|N]
 *         [localport=P] [remoteport=P] [localip=A] [remoteip=A] [edge=yes|no] [group=G]
 *   netsh advfirewall firewall delete rule name=N|all [dir=..] [profile=..] [program=..]
 *         [protocol=..] [localport=..] [remoteport=..]
 *   netsh advfirewall firewall show rule name=N|all [dir=..] [profile=..] [verbose]
 *   netsh advfirewall firewall set rule name=N [dir=..] new [enable=..] [action=..] ...
 *   netsh advfirewall set allprofiles|currentprofile|domainprofile|privateprofile|publicprofile
 *         state on|off | firewallpolicy blockinbound|blockinboundalways|allowinbound,allowoutbound|blockoutbound
 *   netsh advfirewall show allprofiles|currentprofile|domainprofile|privateprofile|publicprofile
 *   netsh advfirewall reset
 *
 * The answers read as Windows' do ("Ok.", "Deleted 2 rule(s).", "No rules
 * match the specified criteria.").
 */

#include "apps.h"
#include "../lib/string.h"
#include "../mm/vmm.h"
#include "../ke/printf.h"
#include "../net/wfp.h"

typedef struct {
    NetshOut out;
    void    *ctx;
} Out;

static void say(Out *o, const char *s) { o->out(o->ctx, false, s); }
static void err(Out *o, const char *s) { o->out(o->ctx, true, s); }

static void sayf(Out *o, const char *fmt, ...)
{
    char buf[400];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    say(o, buf);
}

static bool is(const char *a, const char *b)
{
    while (*a && (*a | 0x20) == (*b | 0x20)) { a++; b++; }
    return !*a && !*b;
}

/* "key=value" pairs, with "quoted values" the terminal split at their
 * spaces put back together */
#define MAX_KV 24
typedef struct { char key[24]; char val[260]; } Kv;

static int parse_kv(int argc, char **argv, int from, Kv *kv, int *bare_new)
{
    int n = 0;
    *bare_new = -1;
    for (int i = from; i < argc && n < MAX_KV; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq) {
            if (is(argv[i], "new")) *bare_new = n;
            else if (n < MAX_KV) { strncpy(kv[n].key, argv[i], sizeof(kv[n].key) - 1); kv[n].key[sizeof(kv[n].key) - 1] = 0; kv[n++].val[0] = 0; }
            continue;
        }
        int kl = (int)(eq - argv[i]);
        if (kl >= (int)sizeof(kv[n].key)) kl = (int)sizeof(kv[n].key) - 1;
        memcpy(kv[n].key, argv[i], (size_t)kl);
        kv[n].key[kl] = 0;
        char *v = kv[n].val;
        int vl = 0, cap = (int)sizeof(kv[n].val);
        const char *s = eq + 1;
        bool quoted = *s == '"';
        if (quoted) s++;
        for (;;) {
            for (; *s && vl < cap - 1; s++) if (*s != '"') v[vl++] = *s;
            size_t L = strlen(argv[i]);
            bool closed = !quoted || (L > 0 && argv[i][L - 1] == '"' && argv[i] + L - 1 > eq + 1);
            if (closed || i + 1 >= argc) break;
            i++;                                           /* (the rest of a quoted value) */
            if (vl < cap - 1) v[vl++] = ' ';
            s = argv[i];
        }
        v[vl] = 0;
        n++;
    }
    return n;
}

static const char *kv_get(const Kv *kv, int from, int to, const char *key)
{
    for (int i = from; i < to; i++) if (is(kv[i].key, key)) return kv[i].val;
    return NULL;
}

static int protocol_of(const char *s)
{
    if (!s || is(s, "any")) return 256;
    if (is(s, "tcp")) return 6;
    if (is(s, "udp")) return 17;
    if (is(s, "icmpv4")) return 1;
    if (is(s, "icmpv6")) return 58;
    int n = 0;
    for (const char *p = s; *p; p++) { if (*p < '0' || *p > '9') return -1; n = n * 10 + (*p - '0'); }
    return n <= 255 ? n : -1;
}

static const char *protocol_name(int p, char *buf)
{
    switch (p) {
    case 256: return "Any";
    case 6:   return "TCP";
    case 17:  return "UDP";
    case 1:   return "ICMPv4";
    case 58:  return "ICMPv6";
    default:  ksnprintf(buf, 8, "%d", p); return buf;
    }
}

static unsigned profiles_of(const char *s)
{
    if (!s || is(s, "any")) return NOVAFW_ALL;
    unsigned m = 0;
    char item[24];
    while (*s) {
        int n = 0;
        while (*s && *s != ',') { if (n < 23) item[n++] = *s; s++; }
        item[n] = 0;
        if (*s) s++;
        if (is(item, "public")) m |= NOVAFW_PUBLIC;
        else if (is(item, "private")) m |= NOVAFW_PRIVATE;
        else if (is(item, "domain")) m |= NOVAFW_DOMAIN;
        else if (is(item, "any")) m |= NOVAFW_ALL;
        else return 0;
    }
    return m;
}

static void profiles_text(unsigned m, char *out)
{
    out[0] = 0;
    if ((m & 7) == 7) { strcpy(out, "Domain,Private,Public"); return; }
    if (m & NOVAFW_DOMAIN) strcat(out, "Domain");
    if (m & NOVAFW_PRIVATE) { if (out[0]) strcat(out, ","); strcat(out, "Private"); }
    if (m & NOVAFW_PUBLIC) { if (out[0]) strcat(out, ","); strcat(out, "Public"); }
}

static INT64 fw(UINT32 op, void *req, UINT32 *hr)
{
    INT64 r = 0;
    *hr = FwCtl(true, op, 0, req, &r);                  /* (the terminal is the system's own shell) */
    return r;
}

/* Fields of a rule from key=value pairs; NULL or the reason it is not valid */
static const char *rule_fields(NovaFwRule *r, const Kv *kv, int from, int to)
{
    const char *v;
    if ((v = kv_get(kv, from, to, "name"))) strncpy(r->name, v, sizeof(r->name) - 1);
    if ((v = kv_get(kv, from, to, "dir"))) {
        if (is(v, "in")) r->dir = 1; else if (is(v, "out")) r->dir = 2; else return "dir must be in or out.";
    }
    if ((v = kv_get(kv, from, to, "action"))) {
        if (is(v, "allow")) r->action = 1; else if (is(v, "block")) r->action = 0;
        else if (is(v, "bypass")) return "action=bypass needs IPsec, which this firewall does not have yet.";
        else return "action must be allow, block or bypass.";
    }
    if ((v = kv_get(kv, from, to, "program"))) strncpy(r->app, v, sizeof(r->app) - 1);
    if ((v = kv_get(kv, from, to, "service"))) strncpy(r->service, v, sizeof(r->service) - 1);
    if ((v = kv_get(kv, from, to, "description"))) strncpy(r->desc, v, sizeof(r->desc) - 1);
    if ((v = kv_get(kv, from, to, "group"))) strncpy(r->group, v, sizeof(r->group) - 1);
    if ((v = kv_get(kv, from, to, "enable"))) r->enabled = is(v, "yes");
    if ((v = kv_get(kv, from, to, "edge"))) r->edge = is(v, "yes");
    if ((v = kv_get(kv, from, to, "profile")) && !(r->profiles = profiles_of(v))) return "profile must be public, private, domain or any.";
    if ((v = kv_get(kv, from, to, "protocol")) && (r->protocol = protocol_of(v)) < 0) return "The protocol is not valid.";
    if ((v = kv_get(kv, from, to, "localport"))) strncpy(r->lports, is(v, "any") ? "" : v, sizeof(r->lports) - 1);
    if ((v = kv_get(kv, from, to, "remoteport"))) strncpy(r->rports, is(v, "any") ? "" : v, sizeof(r->rports) - 1);
    if ((v = kv_get(kv, from, to, "localip"))) strncpy(r->laddrs, is(v, "any") ? "" : v, sizeof(r->laddrs) - 1);
    if ((v = kv_get(kv, from, to, "remoteip"))) strncpy(r->raddrs, is(v, "any") ? "" : v, sizeof(r->raddrs) - 1);
    return NULL;
}

/* Whether rule @r is one the filters (name, dir, profile...) pick */
static bool rule_picked(const NovaFwRule *r, const Kv *kv, int n)
{
    const char *v;
    if ((v = kv_get(kv, 0, n, "name")) && !is(v, "all") && !is(r->name, v)) return false;
    if ((v = kv_get(kv, 0, n, "dir")) && r->dir != (is(v, "in") ? 1 : 2)) return false;
    if ((v = kv_get(kv, 0, n, "profile")) && !is(v, "any") && !(r->profiles & profiles_of(v))) return false;
    if ((v = kv_get(kv, 0, n, "program")) && !is(r->app, v)) return false;
    if ((v = kv_get(kv, 0, n, "protocol")) && r->protocol != protocol_of(v)) return false;
    if ((v = kv_get(kv, 0, n, "localport")) && !is(r->lports, v)) return false;
    if ((v = kv_get(kv, 0, n, "remoteport")) && !is(r->rports, v)) return false;
    return true;
}

static void show_rule(Out *o, const NovaFwRule *r, bool verbose)
{
    char prof[32], pbuf[8];
    say(o, "");
    sayf(o, "Rule Name:                            %s", r->name);
    say(o, "----------------------------------------------------------------------");
    if (verbose && r->desc[0]) sayf(o, "Description:                          %s", r->desc);
    sayf(o, "Enabled:                              %s", r->enabled ? "Yes" : "No");
    sayf(o, "Direction:                            %s", r->dir == 1 ? "In" : "Out");
    profiles_text(r->profiles, prof);
    sayf(o, "Profiles:                             %s", prof);
    sayf(o, "Grouping:                             %s", r->group);
    sayf(o, "LocalIP:                              %s", r->laddrs[0] ? r->laddrs : "Any");
    sayf(o, "RemoteIP:                             %s", r->raddrs[0] ? r->raddrs : "Any");
    sayf(o, "Protocol:                             %s", protocol_name(r->protocol, pbuf));
    if (r->protocol == 6 || r->protocol == 17) {
        sayf(o, "LocalPort:                            %s", r->lports[0] ? r->lports : "Any");
        sayf(o, "RemotePort:                           %s", r->rports[0] ? r->rports : "Any");
    }
    sayf(o, "Edge traversal:                       %s", r->edge ? "Yes" : "No");
    if (verbose) {
        sayf(o, "Program:                              %s", r->app[0] ? r->app : "Any");
        sayf(o, "Service:                              %s", r->service[0] ? r->service : "Any");
    }
    sayf(o, "Action:                               %s", r->action ? "Allow" : "Block");
}

static void cmd_rules(Out *o, int argc, char **argv)
{
    static const char usage[] =
        "Usage: netsh advfirewall firewall add rule name=N dir=in|out action=allow|block [program=PATH]\n"
        "         [protocol=tcp|udp|any] [localport=P] [remoteport=P] [localip=A] [remoteip=A]\n"
        "         [profile=public|private|domain|any] [enable=yes|no] [description=D]\n"
        "       netsh advfirewall firewall delete rule name=N|all [dir=in|out] [profile=..] [program=..]\n"
        "       netsh advfirewall firewall show rule name=N|all [dir=in|out] [profile=..] [verbose]\n"
        "       netsh advfirewall firewall set rule name=N new [enable=yes|no] [action=allow|block] ...";
    if (argc < 5 || !is(argv[4], "rule")) { err(o, usage); return; }
    Kv *kv = kmalloc(sizeof(Kv) * MAX_KV);
    NovaFwRule *r = kmalloc(sizeof(*r));
    if (!kv || !r) { kfree(kv); kfree(r); err(o, "Out of memory."); return; }
    int bare_new, n = parse_kv(argc, argv, 5, kv, &bare_new);
    const char *name = kv_get(kv, 0, n, "name");
    UINT32 hr;
    if (!name) { err(o, usage); goto done; }
    if (is(argv[3], "add")) {
        memset(r, 0, sizeof(*r));
        r->enabled = 1; r->protocol = 256; r->profiles = NOVAFW_ALL; r->dir = 0; r->action = -1;
        const char *why = rule_fields(r, kv, 0, n);
        if (!why && !r->dir) why = "A rule needs dir=in or dir=out.";
        if (!why && r->action < 0) why = "A rule needs action=allow or action=block.";
        if (!why && is(name, "all")) why = "\"all\" is not a name a rule can have.";
        if (why) { err(o, why); goto done; }
        fw(NOVAFWC_RULE_ADD, r, &hr);
        if (hr) { err(o, r->desc[0] ? r->desc : "The rule could not be added."); goto done; }
        say(o, "Ok.");
        say(o, "");
        goto done;
    }
    bool del = is(argv[3], "delete"), show = is(argv[3], "show"), set = is(argv[3], "set");
    if (!del && !show && !set) { err(o, usage); goto done; }
    int sel = set && bare_new >= 0 ? bare_new : n;          /* (set: the pairs before "new" pick, after it change) */
    bool verbose = kv_get(kv, 0, n, "verbose") != NULL;
    int picked = 0;
    INT64 count = fw(NOVAFWC_RULE_COUNT, NULL, &hr);
    for (int i = (int)count - 1; i >= 0; i--) {             /* (backwards: deleting keeps the earlier indices) */
        memset(r, 0, sizeof(*r));
        r->index = show ? (int)count - 1 - i : i;
        fw(NOVAFWC_RULE_AT, r, &hr);
        if (hr || !rule_picked(r, kv, sel)) continue;
        picked++;
        if (show) show_rule(o, r, verbose);
        else if (del) fw(NOVAFWC_RULE_DELETE, r, &hr);
        else {
            const char *why = rule_fields(r, kv, sel, n);
            if (why) { err(o, why); goto done; }
            fw(NOVAFWC_RULE_SET, r, &hr);
            if (hr) { err(o, r->desc[0] ? r->desc : "The rule could not be changed."); goto done; }
        }
    }
    if (!picked) { say(o, "No rules match the specified criteria."); goto done; }
    say(o, "");
    if (del) { sayf(o, "Deleted %d rule(s).", picked); }
    else if (set) { sayf(o, "Updated %d rule(s).", picked); }
    say(o, "Ok.");
    say(o, "");
done:
    kfree(kv);
    kfree(r);
}

static const char *const g_names[3] = { "Domain", "Private", "Public" };
static const UINT32 g_bits[3] = { NOVAFW_DOMAIN, NOVAFW_PRIVATE, NOVAFW_PUBLIC };

/* Which profiles "allprofiles", "currentprofile", ... name (a bit mask of 1, 2, 4) */
static int which(const char *s)
{
    if (is(s, "allprofiles")) return 7;
    if (is(s, "domainprofile")) return 1;
    if (is(s, "privateprofile")) return 2;
    if (is(s, "publicprofile")) return 4;
    if (is(s, "currentprofile")) {
        UINT32 hr;
        INT64 cur = fw(NOVAFWC_CURRENT, NULL, &hr);
        return cur == NOVAFW_DOMAIN ? 1 : cur == NOVAFW_PRIVATE ? 2 : 4;
    }
    return 0;
}

static void show_profiles(Out *o, int mask)
{
    for (int i = 0; i < 3; i++) {
        if (!(mask & (1 << i))) continue;
        NovaFwProfile p;
        memset(&p, 0, sizeof(p));
        p.profile = g_bits[i];
        UINT32 hr;
        fw(NOVAFWC_PROFILE_GET, &p, &hr);
        say(o, "");
        sayf(o, "%s Profile Settings:", g_names[i]);
        say(o, "----------------------------------------------------------------------");
        sayf(o, "State                                 %s", p.enabled ? "ON" : "OFF");
        sayf(o, "Firewall Policy                       %s,%s",
             p.block_all ? "BlockInboundAlways" : p.default_in ? "AllowInbound" : "BlockInbound",
             p.default_out ? "AllowOutbound" : "BlockOutbound");
        say(o, "LocalFirewallRules                    N/A (GPO-store only)");
        say(o, "LocalConSecRules                      N/A (GPO-store only)");
        say(o, "InboundUserNotification               Enable");
        sayf(o, "RemoteManagement                      Disable");
        sayf(o, "UnicastResponseToMulticast            %s", p.unicast_off ? "Disable" : "Enable");
    }
    say(o, "Ok.");
    say(o, "");
}

void NetshAdvfirewall(int argc, char **argv, NetshOut out, void *ctx)
{
    Out o = { out, ctx };
    static const char usage[] =
        "Usage: netsh advfirewall firewall add|delete|show|set rule ...\n"
        "       netsh advfirewall set allprofiles|currentprofile|domainprofile|privateprofile|publicprofile state on|off\n"
        "       netsh advfirewall set ...profile firewallpolicy blockinbound|blockinboundalways|allowinbound,allowoutbound|blockoutbound\n"
        "       netsh advfirewall show allprofiles|currentprofile|domainprofile|privateprofile|publicprofile\n"
        "       netsh advfirewall reset";
    if (argc < 3) { err(&o, usage); return; }
    if (is(argv[2], "firewall")) { cmd_rules(&o, argc, argv); return; }
    UINT32 hr;
    if (is(argv[2], "reset")) {
        fw(NOVAFWC_RESTORE_DEFAULTS, NULL, &hr);
        say(&o, "Ok.");
        say(&o, "");
        return;
    }
    if (is(argv[2], "show")) {
        int m = argc > 3 ? which(argv[3]) : 0;
        if (!m) { err(&o, usage); return; }
        show_profiles(&o, m);
        return;
    }
    if (!is(argv[2], "set") || argc < 6) { err(&o, usage); return; }
    int m = which(argv[3]);
    if (!m) { err(&o, usage); return; }
    for (int i = 0; i < 3; i++) {
        if (!(m & (1 << i))) continue;
        NovaFwProfile p;
        memset(&p, 0, sizeof(p));
        p.profile = g_bits[i];
        fw(NOVAFWC_PROFILE_GET, &p, &hr);
        if (is(argv[4], "state")) {
            if (is(argv[5], "on")) p.enabled = 1;
            else if (is(argv[5], "off")) p.enabled = 0;
            else { err(&o, "state must be on or off."); return; }
        } else if (is(argv[4], "firewallpolicy")) {
            char pol[64];
            strncpy(pol, argv[5], sizeof(pol) - 1);
            pol[sizeof(pol) - 1] = 0;
            char *comma = strchr(pol, ',');
            if (!comma) { err(&o, "firewallpolicy is inbound,outbound (blockinbound,allowoutbound)."); return; }
            *comma = 0;
            const char *in = pol, *outp = comma + 1;
            if (is(in, "blockinbound")) { p.default_in = 0; p.block_all = 0; }
            else if (is(in, "blockinboundalways")) { p.default_in = 0; p.block_all = 1; }
            else if (is(in, "allowinbound")) { p.default_in = 1; p.block_all = 0; }
            else if (!is(in, "notconfigured")) { err(&o, "The inbound policy is not valid."); return; }
            if (is(outp, "allowoutbound")) p.default_out = 1;
            else if (is(outp, "blockoutbound")) p.default_out = 0;
            else if (!is(outp, "notconfigured")) { err(&o, "The outbound policy is not valid."); return; }
        } else { err(&o, usage); return; }
        fw(NOVAFWC_PROFILE_SET, &p, &hr);
    }
    say(&o, "Ok.");
    say(&o, "");
}
