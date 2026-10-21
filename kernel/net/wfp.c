/*
 * wfp.c — the filtering engine (see wfp.h)
 *
 * Objects are as the Windows Filtering Platform's Base Filtering Engine
 * keeps them: providers, sublayers (each with a weight) and filters, each
 * filter at one layer in one sublayer with a weight, an action (block or
 * permit) and conditions.  A classification walks the layer's sublayers
 * from the heaviest: in each, the heaviest filter whose conditions all
 * match decides that sublayer; a block anywhere wins over a permit, except
 * a "hard" permit (FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT), which no lighter
 * sublayer can overturn; with no decision at all the traffic passes.  The
 * firewall is the sublayer FWPM_SUBLAYER_MPSSVC_WF, which firewall.c
 * decides from its rules.  Conditions on the same field are alternatives,
 * on different fields all must match (as in WFP).
 *
 * Sessions: a program's FwpmEngineOpen0; the objects a dynamic session
 * adds go when it closes (or its process ends).  A transaction's
 * additions take effect at its commit and its deletions too; an abort
 * undoes both.  Persistent objects (FWPM_*_FLAG_PERSISTENT) are kept in
 * the registry under the Base Filtering Engine's key and come back at
 * boot.  Callouts and the layers beyond the ten in novafw.h are not here.
 */

#include "wfp.h"
#include "net_internal.h"
#include "../ke/printf.h"
#include "../mm/vmm.h"
#include "../lib/string.h"
#include "../um/um.h"

#define MAX_FILTERS   512
#define MAX_SUBLAYERS 64
#define MAX_PROVIDERS 32
#define MAX_SESSIONS  64
#define MAX_APPS      256

#define BFE_KEY "Machine\\SYSTEM\\CurrentControlSet\\Services\\BFE\\Parameters\\Policy\\Persistent"

typedef struct {
    NovaWfpFilter f;
    UINT32 session;          /* the session that added it (0: none, a persistent one from boot) */
    UINT32 add_txn;          /* added in this session's open transaction: not in force yet */
    UINT32 del_txn;          /* deleted in this session's open transaction: still in force */
    bool   dynamic;          /* goes with its session */
} Filter;

typedef struct {
    NovaWfpSublayer s;
    bool   used, builtin, dynamic;
    UINT32 session, add_txn, del_txn;
} Sublayer;

typedef struct {
    NovaWfpProvider p;
    bool   used, dynamic;
    UINT32 session, add_txn, del_txn;
} Provider;

typedef struct {
    bool   used, dynamic, txn;
    UINT32 pid;
} Session;

static Filter  *g_filters[MAX_FILTERS];
static Sublayer g_sub[MAX_SUBLAYERS];
static Provider g_prov[MAX_PROVIDERS];
static Session  g_sess[MAX_SESSIONS];
static int      g_layer_use[NOVAWFP_LAYERS];
static UINT64   g_next_id = 66000;
static char    *g_apps[MAX_APPS];

/* FWPM_SUBLAYER_UNIVERSAL (filters that name no sublayer) and
 * FWPM_SUBLAYER_MPSSVC_WF (the firewall's) */
static const NovaGuid UNIVERSAL = { { 0x03, 0xcc, 0xbe, 0xee, 0xd4, 0xce, 0x80, 0x43,
                                      0x81, 0x9a, 0x27, 0x34, 0x39, 0x7b, 0x2b, 0x74 } };
static const NovaGuid MPSSVC_WF = { { 0x41, 0xd4, 0xcd, 0xb3, 0x90, 0xaf, 0xba, 0x41,
                                      0xa7, 0x45, 0x7c, 0x60, 0x08, 0xff, 0x23, 0x01 } };
#define UNIVERSAL_WEIGHT 0x8000u
#define FIREWALL_WEIGHT  0x7FFEu

static bool guid_eq(const NovaGuid *a, const NovaGuid *b) { return !memcmp(a, b, sizeof(*a)); }
static bool guid_zero(const NovaGuid *a) { static const NovaGuid z; return guid_eq(a, &z); }

static void guid_text(const NovaGuid *g, char *out, int cap)
{
    const UINT8 *b = g->b;
    ksnprintf(out, (size_t)cap, "{%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
              b[3], b[2], b[1], b[0], b[5], b[4], b[7], b[6], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

static void builtins(void)
{
    if (g_sub[0].used) return;
    g_sub[0].used = g_sub[0].builtin = true;
    g_sub[0].s.key = UNIVERSAL;
    g_sub[0].s.weight = UNIVERSAL_WEIGHT;
    strcpy(g_sub[0].s.name, "Universal sublayer");
    g_sub[1].used = g_sub[1].builtin = true;
    g_sub[1].s.key = MPSSVC_WF;
    g_sub[1].s.weight = FIREWALL_WEIGHT;
    strcpy(g_sub[1].s.name, "Windows Defender Firewall");
}

/* ---------------------------------------------------------------------------
 * Application ids
 * ------------------------------------------------------------------------- */
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static bool starts_ci(const char *s, const char *p)
{
    for (; *p; s++, p++) if (lower(*s) != lower(*p)) return false;
    return true;
}

void WfpAppNormalize(const char *path, char *out, int cap)
{
    static const struct { const char *var, *val; } env[] = {
        { "%systemroot%", "\\windows" }, { "%windir%", "\\windows" },
        { "%programfiles(x86)%", "\\program files (x86)" }, { "%programfiles%", "\\program files" },
        { "%systemdrive%", "" }, { "%programdata%", "\\programdata" },
    };
    int n = 0;
    if (!path) path = "";
    if (starts_ci(path, "\\\\?\\") || starts_ci(path, "\\??\\")) path += 4;
    if (starts_ci(path, "\\device\\")) {                     /* \device\harddiskvolumeN\... */
        const char *s = strchr(path + 8, '\\');
        path = s ? s : "";
    } else if (path[0] && path[1] == ':') path += 2;
    for (unsigned i = 0; i < sizeof env / sizeof env[0]; i++) {
        if (!starts_ci(path, env[i].var)) continue;
        for (const char *v = env[i].val; *v && n < cap - 1; v++) out[n++] = *v;
        path += strlen(env[i].var);
        break;
    }
    for (; *path && n < cap - 1; path++) out[n++] = *path == '/' ? '\\' : lower(*path);
    out[n] = 0;
}

UINT16 WfpAppIntern(const char *path)
{
    char norm[NOVAWFP_APP_MAX];
    WfpAppNormalize(path, norm, sizeof(norm));
    if (!norm[0]) return 0;
    int freei = -1;
    for (int i = 0; i < MAX_APPS; i++) {
        if (!g_apps[i]) { if (freei < 0) freei = i; continue; }
        if (!strcmp(g_apps[i], norm)) return (UINT16)(i + 1);
    }
    if (freei < 0) return 0;                                   /* (full: the program counts as no app) */
    g_apps[freei] = strdup(norm);
    return g_apps[freei] ? (UINT16)(freei + 1) : 0;
}

const char *WfpAppName(UINT16 id)
{
    return id && id <= MAX_APPS && g_apps[id - 1] ? g_apps[id - 1] : "";
}

/* ---------------------------------------------------------------------------
 * Classification
 * ------------------------------------------------------------------------- */
static bool num_match(UINT8 match, UINT64 v, UINT64 lo, UINT64 hi)
{
    switch (match) {
    case NOVAWFP_MATCH_EQUAL:            return v == lo;
    case NOVAWFP_MATCH_NOT_EQUAL:        return v != lo;
    case NOVAWFP_MATCH_GREATER:          return v > lo;
    case NOVAWFP_MATCH_LESS:             return v < lo;
    case NOVAWFP_MATCH_GREATER_OR_EQUAL: return v >= lo;
    case NOVAWFP_MATCH_LESS_OR_EQUAL:    return v <= lo;
    case NOVAWFP_MATCH_RANGE:            return v >= lo && v <= hi;
    case NOVAWFP_MATCH_FLAGS_ALL_SET:    return (v & lo) == lo;
    case NOVAWFP_MATCH_FLAGS_ANY_SET:    return (v & lo) != 0;
    case NOVAWFP_MATCH_FLAGS_NONE_SET:   return (v & lo) == 0;
    default:                             return false;
    }
}

static bool prefix_match(const UINT8 *a, const UINT8 *net, int bits)
{
    for (int i = 0; bits > 0; i++, bits -= 8) {
        UINT8 m = bits >= 8 ? 0xFF : (UINT8)(0xFF << (8 - bits));
        if ((a[i] & m) != (net[i] & m)) return false;
    }
    return true;
}

static bool addr_match(const NovaWfpCond *c, const WfpValues *v, const UINT8 *a)
{
    bool hit;
    if (c->kind == NOVAWFP_V_ADDR4 || c->kind == NOVAWFP_V_ADDR6) {
        if ((c->kind == NOVAWFP_V_ADDR6) != v->v6) hit = false;
        else hit = prefix_match(a, c->addr, c->prefix);
    } else {                                                   /* an IPv4 address as a number (host order) */
        if (v->v6) return c->match == NOVAWFP_MATCH_NOT_EQUAL;
        UINT64 n = ((UINT64)a[0] << 24) | ((UINT64)a[1] << 16) | ((UINT64)a[2] << 8) | a[3];
        return num_match(c->match, n, c->lo, c->hi);
    }
    return c->match == NOVAWFP_MATCH_NOT_EQUAL ? !hit : hit;
}

static bool cond_match(const NovaWfpCond *c, const WfpValues *v)
{
    switch (c->field) {
    case NOVAWFP_F_PROTOCOL:    return num_match(c->match, v->protocol, c->lo, c->hi);
    case NOVAWFP_F_LOCAL_PORT:  return num_match(c->match, v->lport, c->lo, c->hi);
    case NOVAWFP_F_REMOTE_PORT: return num_match(c->match, v->rport, c->lo, c->hi);
    case NOVAWFP_F_FLAGS:       return num_match(c->match, v->flags, c->lo, c->hi);
    case NOVAWFP_F_IF_INDEX:    return num_match(c->match, v->if_index, c->lo, c->hi);
    case NOVAWFP_F_IF_LUID:     return num_match(c->match, v->if_luid, c->lo, c->hi);
    case NOVAWFP_F_LOCAL_ADDR:  return addr_match(c, v, v->local);
    case NOVAWFP_F_REMOTE_ADDR: return addr_match(c, v, v->remote);
    case NOVAWFP_F_APP_ID: {
        bool eq = v->app && !strcmp(c->app, v->app);
        return c->match == NOVAWFP_MATCH_NOT_EQUAL ? !eq : eq;
    }
    default:                    return false;
    }
}

/* Every field the filter names must have one of its conditions match */
static bool filter_match(const NovaWfpFilter *f, const WfpValues *v)
{
    for (UINT32 i = 0; i < f->ncond; i++) {
        bool seen = false, any = false;
        for (UINT32 j = 0; j < i; j++) if (f->cond[j].field == f->cond[i].field) seen = true;
        if (seen) continue;                                    /* (that field was settled already) */
        for (UINT32 j = i; j < f->ncond && !any; j++)
            if (f->cond[j].field == f->cond[i].field && cond_match(&f->cond[j], v)) any = true;
        if (!any) return false;
    }
    return true;
}

static bool in_force(const Filter *f)
{
    return !f->add_txn && !(f->f.flags & NOVAWFP_FILTER_DISABLED);
}

bool WfpLayerUsed(int layer) { return layer >= 0 && layer < NOVAWFP_LAYERS && g_layer_use[layer] > 0; }

/* One sublayer's decision: -1 none, else the heaviest matching filter's
 * action; *hard for a hard permit */
static int sublayer_decide(const NovaGuid *key, const WfpValues *v, bool *hard)
{
    const Filter *best = NULL;
    for (int i = 0; i < MAX_FILTERS; i++) {
        const Filter *f = g_filters[i];
        if (!f || (int)f->f.layer != v->layer || !in_force(f) || !guid_eq(&f->f.sublayer, key)) continue;
        if (best && (f->f.weight < best->f.weight || (f->f.weight == best->f.weight && f->f.id > best->f.id))) continue;
        if (filter_match(&f->f, v)) best = f;
    }
    if (!best) return -1;
    *hard = best->f.action == NOVAWFP_ACTION_BLOCK || (best->f.flags & NOVAWFP_FILTER_CLEAR_ACTION_RIGHT);
    return best->f.action == NOVAWFP_ACTION_PERMIT ? 1 : 0;
}

bool WfpClassify(const WfpValues *v)
{
    builtins();
    bool done[MAX_SUBLAYERS] = { 0 }, permit = true;
    for (;;) {                                                 /* the sublayers, heaviest first */
        int pick = -1;
        for (int i = 0; i < MAX_SUBLAYERS; i++)
            if (g_sub[i].used && !done[i] && !g_sub[i].add_txn &&
                (pick < 0 || g_sub[i].s.weight > g_sub[pick].s.weight)) pick = i;
        if (pick < 0) break;
        done[pick] = true;
        bool hard = false;
        int d = guid_eq(&g_sub[pick].s.key, &MPSSVC_WF) ? FwDecide(v) : sublayer_decide(&g_sub[pick].s.key, v, &hard);
        if (d < 0) continue;
        if (d == 0) return false;                              /* a block wins over the permits so far */
        if (hard) return true;
        permit = true;
    }
    return permit;
}

/* ---------------------------------------------------------------------------
 * Objects
 * ------------------------------------------------------------------------- */
static Session *session_of(UINT32 pid, UINT32 s)
{
    if (s == 0 || s >= MAX_SESSIONS || !g_sess[s].used || g_sess[s].pid != pid) return NULL;
    return &g_sess[s];
}

static Filter *filter_by_id(UINT64 id)
{
    for (int i = 0; i < MAX_FILTERS; i++) if (g_filters[i] && g_filters[i]->f.id == id) return g_filters[i];
    return NULL;
}

static Filter *filter_by_key(const NovaGuid *k)
{
    for (int i = 0; i < MAX_FILTERS; i++) if (g_filters[i] && guid_eq(&g_filters[i]->f.key, k)) return g_filters[i];
    return NULL;
}

static Sublayer *sublayer_by_key(const NovaGuid *k)
{
    for (int i = 0; i < MAX_SUBLAYERS; i++) if (g_sub[i].used && guid_eq(&g_sub[i].s.key, k)) return &g_sub[i];
    return NULL;
}

static Provider *provider_by_key(const NovaGuid *k)
{
    for (int i = 0; i < MAX_PROVIDERS; i++) if (g_prov[i].used && guid_eq(&g_prov[i].p.key, k)) return &g_prov[i];
    return NULL;
}

static void new_key(NovaGuid *g)
{
    extern int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);
    size_t got = 0;
    mbedtls_hardware_poll(NULL, g->b, sizeof(g->b), &got);
    g->b[7] = (UINT8)((g->b[7] & 0x0F) | 0x40);                /* a version 4 GUID */
    g->b[8] = (UINT8)((g->b[8] & 0x3F) | 0x80);
}

void WfpNewKeyText(char *out, int cap)
{
    NovaGuid g;
    new_key(&g);
    guid_text(&g, out, cap);
}

static void filter_free(int i)
{
    Filter *f = g_filters[i];
    if (!f) return;
    if (f->f.layer < NOVAWFP_LAYERS) g_layer_use[f->f.layer]--;
    g_filters[i] = NULL;
    kfree(f);
}

static UINT64 effective_weight(const NovaWfpFilter *f)
{
    UINT64 auto_w = (UINT64)f->ncond << 52;
    if (f->weight_type == 1) return ((f->weight & 0xF) << 60) | auto_w;
    if (f->weight_type == 4) return f->weight;
    return auto_w;
}

/* Changes the registry needs once the network lock is let go */
typedef struct {
    int  kind;               /* 0 none, 1 save filter, 2 delete filter, 3 save sublayer, 4 delete sublayer, 5 save provider, 6 delete provider */
    NovaGuid key;
    union { NovaWfpFilter f; NovaWfpSublayer s; NovaWfpProvider p; } u;
} Persist;

static UINT32 filter_add(Session *ss, UINT32 sid, NovaWfpFilter *in, Persist *pe)
{
    if (in->layer >= NOVAWFP_LAYERS) return NOVAWFP_E_LAYER_NOT_FOUND;
    if (in->action != NOVAWFP_ACTION_BLOCK && in->action != NOVAWFP_ACTION_PERMIT)
        return (in->action & 0x4000) ? NOVAWFP_E_CALLOUT_NOT_FOUND : NOVAWFP_E_INVALID_ACTION_TYPE;
    if (in->weight_type != 0 && in->weight_type != 1 && in->weight_type != 4) return NOVAWFP_E_INVALID_WEIGHT;
    if (in->weight_type == 1 && in->weight > 15) return NOVAWFP_E_INVALID_WEIGHT;
    if (in->ncond > NOVAWFP_MAX_COND) return NOVAWFP_E_INVALID_PARAMETER;
    if (guid_zero(&in->sublayer)) in->sublayer = UNIVERSAL;
    if (!sublayer_by_key(&in->sublayer)) return NOVAWFP_E_SUBLAYER_NOT_FOUND;
    if (in->has_provider && !provider_by_key(&in->provider)) return NOVAWFP_E_PROVIDER_NOT_FOUND;
    if (guid_zero(&in->key)) new_key(&in->key);
    else if (filter_by_key(&in->key)) return NOVAWFP_E_ALREADY_EXISTS;
    int slot = -1;
    for (int i = 0; i < MAX_FILTERS && slot < 0; i++) if (!g_filters[i]) slot = i;
    if (slot < 0) return NOVAWFP_E_OUTOFMEMORY;
    Filter *f = kzalloc(sizeof(*f));
    if (!f) return NOVAWFP_E_OUTOFMEMORY;
    in->name[sizeof(in->name) - 1] = in->desc[sizeof(in->desc) - 1] = 0;
    for (UINT32 i = 0; i < in->ncond; i++) {
        NovaWfpCond *c = &in->cond[i];
        c->app[NOVAWFP_APP_MAX - 1] = 0;
        if (c->field == NOVAWFP_F_APP_ID) {                    /* (FwpmGetAppIdFromFileName0's \device\... path, or a DOS one) */
            char norm[NOVAWFP_APP_MAX];
            WfpAppNormalize(c->app, norm, sizeof(norm));
            strcpy(c->app, norm);
        }
    }
    in->id = g_next_id++;
    f->f = *in;
    f->f.weight = effective_weight(in);
    in->weight = f->f.weight;
    f->session = sid;
    f->dynamic = ss && ss->dynamic && !(in->flags & NOVAWFP_FILTER_PERSISTENT);
    f->add_txn = ss && ss->txn ? sid : 0;
    g_filters[slot] = f;
    g_layer_use[in->layer]++;
    if (in->flags & NOVAWFP_FILTER_PERSISTENT) { pe->kind = 1; pe->key = in->key; pe->u.f = f->f; }
    return 0;
}

static UINT32 filter_delete(Session *ss, UINT32 sid, Filter *f, Persist *pe)
{
    if (!f) return NOVAWFP_E_FILTER_NOT_FOUND;
    if (f->f.flags & NOVAWFP_FILTER_PERSISTENT) { pe->kind = 2; pe->key = f->f.key; }
    if (ss && ss->txn && !f->add_txn) { f->del_txn = sid; return 0; }
    for (int i = 0; i < MAX_FILTERS; i++) if (g_filters[i] == f) filter_free(i);
    return 0;
}

static bool sublayer_in_use(const NovaGuid *k)
{
    for (int i = 0; i < MAX_FILTERS; i++) if (g_filters[i] && guid_eq(&g_filters[i]->f.sublayer, k)) return true;
    return false;
}

/* A transaction ends: commit keeps its changes, abort undoes them */
static void txn_end(UINT32 sid, bool commit)
{
    for (int i = 0; i < MAX_FILTERS; i++) {
        Filter *f = g_filters[i];
        if (!f) continue;
        if (f->add_txn == sid) { if (commit) f->add_txn = 0; else filter_free(i); }
        else if (f->del_txn == sid) { if (commit) filter_free(i); else f->del_txn = 0; }
    }
    for (int i = 0; i < MAX_SUBLAYERS; i++) {
        Sublayer *s = &g_sub[i];
        if (!s->used) continue;
        if (s->add_txn == sid) { if (commit) s->add_txn = 0; else s->used = false; }
        else if (s->del_txn == sid) { if (commit) s->used = false; else s->del_txn = 0; }
    }
    for (int i = 0; i < MAX_PROVIDERS; i++) {
        Provider *p = &g_prov[i];
        if (!p->used) continue;
        if (p->add_txn == sid) { if (commit) p->add_txn = 0; else p->used = false; }
        else if (p->del_txn == sid) { if (commit) p->used = false; else p->del_txn = 0; }
    }
}

static void session_close(UINT32 sid)
{
    Session *ss = &g_sess[sid];
    if (ss->txn) txn_end(sid, false);
    for (int i = 0; i < MAX_FILTERS; i++)
        if (g_filters[i] && g_filters[i]->dynamic && g_filters[i]->session == sid) filter_free(i);
    for (int i = 0; i < MAX_SUBLAYERS; i++)
        if (g_sub[i].used && g_sub[i].dynamic && g_sub[i].session == sid) g_sub[i].used = false;
    for (int i = 0; i < MAX_PROVIDERS; i++)
        if (g_prov[i].used && g_prov[i].dynamic && g_prov[i].session == sid) g_prov[i].used = false;
    memset(ss, 0, sizeof(*ss));
}

static UINT32 ctl_locked(UINT32 pid, bool admin, UINT32 op, UINT32 sid, void *req, Persist *pe)
{
    builtins();
    if (op == NOVAWFP_OPEN) {
        for (UINT32 i = 1; i < MAX_SESSIONS; i++) {
            if (g_sess[i].used) continue;
            g_sess[i].used = true;
            g_sess[i].pid = pid;
            g_sess[i].dynamic = (*(UINT32 *)req & NOVAWFP_SESSION_DYNAMIC) != 0;
            *(UINT32 *)req = i;
            return 0;
        }
        return NOVAWFP_E_OUTOFMEMORY;
    }
    Session *ss = session_of(pid, sid);
    if (!ss) return 0x80070006u;                               /* E_HANDLE */
    bool changes = op == NOVAWFP_FILTER_ADD || op == NOVAWFP_FILTER_DELETE_ID || op == NOVAWFP_FILTER_DELETE_KEY ||
                   op == NOVAWFP_SUBLAYER_ADD || op == NOVAWFP_SUBLAYER_DELETE ||
                   op == NOVAWFP_PROVIDER_ADD || op == NOVAWFP_PROVIDER_DELETE || op == NOVAWFP_TXN_BEGIN;
    if (changes && !admin) return NOVAWFP_E_ACCESS_DENIED;
    switch (op) {
    case NOVAWFP_CLOSE: session_close(sid); return 0;
    case NOVAWFP_TXN_BEGIN:
        if (ss->txn) return NOVAWFP_E_TXN_IN_PROGRESS;
        ss->txn = true;
        return 0;
    case NOVAWFP_TXN_COMMIT:
    case NOVAWFP_TXN_ABORT:
        if (!ss->txn) return NOVAWFP_E_NO_TXN_IN_PROGRESS;
        txn_end(sid, op == NOVAWFP_TXN_COMMIT);
        ss->txn = false;
        return 0;
    case NOVAWFP_FILTER_ADD: return filter_add(ss, sid, req, pe);
    case NOVAWFP_FILTER_DELETE_ID: return filter_delete(ss, sid, filter_by_id(*(UINT64 *)req), pe);
    case NOVAWFP_FILTER_DELETE_KEY: return filter_delete(ss, sid, filter_by_key(req), pe);
    case NOVAWFP_FILTER_GET_ID:
    case NOVAWFP_FILTER_GET_KEY: {
        NovaWfpFilter *o = req;
        Filter *f = op == NOVAWFP_FILTER_GET_ID ? filter_by_id(o->id) : filter_by_key(&o->key);
        if (!f) return NOVAWFP_E_FILTER_NOT_FOUND;
        *o = f->f;
        return 0;
    }
    case NOVAWFP_FILTER_AT: {
        NovaWfpFilter *o = req;
        UINT64 want = o->id, n = 0;
        for (int i = 0; i < MAX_FILTERS; i++) {
            if (!g_filters[i] || g_filters[i]->add_txn) continue;
            if (n++ == want) { *o = g_filters[i]->f; return 0; }
        }
        return NOVAWFP_E_NOT_FOUND;
    }
    case NOVAWFP_SUBLAYER_ADD: {
        NovaWfpSublayer *in = req;
        if (in->weight > 0xFFFF) return NOVAWFP_E_INVALID_WEIGHT;
        if (guid_zero(&in->key)) new_key(&in->key);
        else if (sublayer_by_key(&in->key)) return NOVAWFP_E_ALREADY_EXISTS;
        if (in->has_provider && !provider_by_key(&in->provider)) return NOVAWFP_E_PROVIDER_NOT_FOUND;
        for (int i = 0; i < MAX_SUBLAYERS; i++) {
            if (g_sub[i].used) continue;
            memset(&g_sub[i], 0, sizeof(g_sub[i]));
            in->name[sizeof(in->name) - 1] = 0;
            g_sub[i].s = *in;
            g_sub[i].used = true;
            g_sub[i].session = sid;
            g_sub[i].dynamic = ss->dynamic && !(in->flags & 1);
            g_sub[i].add_txn = ss->txn ? sid : 0;
            if (in->flags & 1) { pe->kind = 3; pe->key = in->key; pe->u.s = *in; }
            return 0;
        }
        return NOVAWFP_E_OUTOFMEMORY;
    }
    case NOVAWFP_SUBLAYER_DELETE: {
        Sublayer *s = sublayer_by_key(req);
        if (!s) return NOVAWFP_E_SUBLAYER_NOT_FOUND;
        if (s->builtin) return NOVAWFP_E_ACCESS_DENIED;
        if (sublayer_in_use(req)) return NOVAWFP_E_IN_USE;
        if (s->s.flags & 1) { pe->kind = 4; pe->key = s->s.key; }
        if (ss->txn && !s->add_txn) s->del_txn = sid;
        else s->used = false;
        return 0;
    }
    case NOVAWFP_SUBLAYER_GET:
    case NOVAWFP_SUBLAYER_AT: {
        NovaWfpSublayer *o = req;
        Sublayer *s = NULL;
        if (op == NOVAWFP_SUBLAYER_GET) s = sublayer_by_key(&o->key);
        else for (int i = 0, n = 0; i < MAX_SUBLAYERS && !s; i++)
            if (g_sub[i].used && !g_sub[i].add_txn && (UINT32)n++ == o->index) s = &g_sub[i];
        if (!s) return op == NOVAWFP_SUBLAYER_GET ? NOVAWFP_E_SUBLAYER_NOT_FOUND : NOVAWFP_E_NOT_FOUND;
        UINT32 idx = o->index;
        *o = s->s;
        o->index = idx;
        return 0;
    }
    case NOVAWFP_PROVIDER_ADD: {
        NovaWfpProvider *in = req;
        if (guid_zero(&in->key)) new_key(&in->key);
        else if (provider_by_key(&in->key)) return NOVAWFP_E_ALREADY_EXISTS;
        for (int i = 0; i < MAX_PROVIDERS; i++) {
            if (g_prov[i].used) continue;
            memset(&g_prov[i], 0, sizeof(g_prov[i]));
            in->name[sizeof(in->name) - 1] = 0;
            g_prov[i].p = *in;
            g_prov[i].used = true;
            g_prov[i].session = sid;
            g_prov[i].dynamic = ss->dynamic && !(in->flags & 1);
            g_prov[i].add_txn = ss->txn ? sid : 0;
            if (in->flags & 1) { pe->kind = 5; pe->key = in->key; pe->u.p = *in; }
            return 0;
        }
        return NOVAWFP_E_OUTOFMEMORY;
    }
    case NOVAWFP_PROVIDER_DELETE: {
        Provider *p = provider_by_key(req);
        if (!p) return NOVAWFP_E_PROVIDER_NOT_FOUND;
        for (int i = 0; i < MAX_FILTERS; i++)
            if (g_filters[i] && g_filters[i]->f.has_provider && guid_eq(&g_filters[i]->f.provider, req)) return NOVAWFP_E_IN_USE;
        if (p->p.flags & 1) { pe->kind = 6; pe->key = p->p.key; }
        if (ss->txn && !p->add_txn) p->del_txn = sid;
        else p->used = false;
        return 0;
    }
    case NOVAWFP_PROVIDER_GET:
    case NOVAWFP_PROVIDER_AT: {
        NovaWfpProvider *o = req;
        Provider *p = NULL;
        if (op == NOVAWFP_PROVIDER_GET) p = provider_by_key(&o->key);
        else for (int i = 0, n = 0; i < MAX_PROVIDERS && !p; i++)
            if (g_prov[i].used && !g_prov[i].add_txn && (UINT32)n++ == o->index) p = &g_prov[i];
        if (!p) return op == NOVAWFP_PROVIDER_GET ? NOVAWFP_E_PROVIDER_NOT_FOUND : NOVAWFP_E_NOT_FOUND;
        UINT32 idx = o->index;
        *o = p->p;
        o->index = idx;
        return 0;
    }
    default: return NOVAWFP_E_INVALID_PARAMETER;
    }
}

static void persist(const Persist *pe)
{
    static const char *const sub[] = { 0, "Filter", "Filter", "SubLayer", "SubLayer", "Provider", "Provider" };
    if (!pe->kind) return;
    char path[160], name[48];
    ksnprintf(path, sizeof(path), BFE_KEY "\\%s", sub[pe->kind]);
    guid_text(&pe->key, name, sizeof(name));
    switch (pe->kind) {
    case 1: um_registry_set_bin(path, name, &pe->u.f, sizeof(pe->u.f)); break;
    case 3: um_registry_set_bin(path, name, &pe->u.s, sizeof(pe->u.s)); break;
    case 5: um_registry_set_bin(path, name, &pe->u.p, sizeof(pe->u.p)); break;
    default: um_registry_delete_value(path, name); break;
    }
}

UINT32 WfpCtl(UINT32 pid, bool admin, UINT32 op, UINT32 session, void *req)
{
    Persist *pe = kzalloc(sizeof(*pe));
    if (!pe) return NOVAWFP_E_OUTOFMEMORY;
    net_lock();
    UINT32 r = ctl_locked(pid, admin, op, session, req, pe);
    net_unlock();
    if (!r) persist(pe);
    kfree(pe);
    return r;
}

void WfpProcessGone(UINT32 pid)
{
    net_lock();
    for (UINT32 i = 1; i < MAX_SESSIONS; i++) if (g_sess[i].used && g_sess[i].pid == pid) session_close(i);
    net_unlock();
}

/* The persistent objects, providers first (sublayers and filters name them) */
void WfpRestore(void)
{
    static const char *const kinds[] = { "Provider", "SubLayer", "Filter" };
    NovaWfpFilter *f = kzalloc(sizeof(*f));
    if (!f) return;
    int n = 0;
    for (int k = 0; k < 3; k++) {
        char path[160];
        ksnprintf(path, sizeof(path), BFE_KEY "\\%s", kinds[k]);
        UINT16 name[64];
        for (UINT32 i = 0;; i++) {
            int nlen; UINT32 type;
            int len = um_registry_enum_value(path, i, name, 64, &nlen, &type, f, sizeof(*f));
            if (len < 0) break;
            Persist pe;
            UINT32 one = 0;
            net_lock();
            builtins();
            if (k == 0 && len == (int)sizeof(NovaWfpProvider)) {
                ctl_locked(0, true, NOVAWFP_OPEN, 0, &one, &pe);   /* (a session of the system's, closed at once: a persistent object stays) */
                ctl_locked(0, true, NOVAWFP_PROVIDER_ADD, one, f, &pe);
            } else if (k == 1 && len == (int)sizeof(NovaWfpSublayer)) {
                ctl_locked(0, true, NOVAWFP_OPEN, 0, &one, &pe);
                ctl_locked(0, true, NOVAWFP_SUBLAYER_ADD, one, f, &pe);
            } else if (k == 2 && len == (int)sizeof(NovaWfpFilter)) {
                ctl_locked(0, true, NOVAWFP_OPEN, 0, &one, &pe);
                if (!ctl_locked(0, true, NOVAWFP_FILTER_ADD, one, f, &pe)) n++;
            }
            if (one) session_close(one);
            net_unlock();
        }
    }
    kfree(f);
    if (n) kprintf("[WFP] %d persistent filter(s) restored\n", n);
}
