/*
 * fwpuclnt.dll — the Windows Filtering Platform's management API: engine
 * sessions, transactions, providers, sublayers and filters
 *
 * On Windows this DLL talks to the Base Filtering Engine service (BFE) by
 * RPC.  NovaOS's engine lives in its kernel beside the network stack it
 * filters (kernel/net/wfp.c), and this DLL asks it through NtNovaSockCtl
 * op 22 (userland/include/novafw.h).  What the engine has:
 *
 *   - the layers ALE_AUTH_CONNECT, ALE_AUTH_RECV_ACCEPT, ALE_AUTH_LISTEN,
 *     INBOUND_TRANSPORT and OUTBOUND_TRANSPORT, V4 and V6;
 *   - the conditions IP_PROTOCOL, IP_LOCAL/REMOTE_ADDRESS (an address, a
 *     V4 address and mask, a V6 prefix), IP_LOCAL/REMOTE_PORT (a number or
 *     a range), ALE_APP_ID (FwpmGetAppIdFromFileName0's blob), FLAGS
 *     (IS_LOOPBACK), INTERFACE_INDEX and IP_LOCAL_INTERFACE, with the
 *     EQUAL ... NOT_EQUAL match types;
 *   - block and permit actions, weights (empty, UINT8, UINT64), hard
 *     permits (FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT), disabled filters,
 *     persistent objects (kept in the registry), dynamic sessions.
 *
 * Callouts (FwpmCalloutAdd0 and the callout actions) need a kernel-mode
 * callout driver and are not here yet, nor net events, IPsec and the
 * layers not listed.  Changing the engine needs an administrator, as on
 * Windows (ERROR_ACCESS_DENIED).
 */
#define FWPUCLNT_BUILD
#include <fwpmu.h>
#include <winternl.h>
#include "../include/novafw.h"

#ifndef ERROR_BAD_NETPATH
#define ERROR_BAD_NETPATH 53
#endif

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

#define ENGINE_MAGIC 0x45504657u          /* "WFPE" */
#define ENUM_MAGIC   0x4D554E45u          /* "ENUM" */

typedef struct { DWORD magic; UINT32 session; } Engine;
typedef struct { DWORD magic; UINT32 next; int layer; BOOL any_layer; BOOL subl; GUID provider; BOOL by_provider; } EnumHandle;

static void *zalloc(SIZE_T n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
static void zfree(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

static const GUID *const g_layers[NOVAWFP_LAYERS] = {
    &FWPM_LAYER_INBOUND_TRANSPORT_V4, &FWPM_LAYER_INBOUND_TRANSPORT_V6,
    &FWPM_LAYER_OUTBOUND_TRANSPORT_V4, &FWPM_LAYER_OUTBOUND_TRANSPORT_V6,
    &FWPM_LAYER_ALE_AUTH_CONNECT_V4, &FWPM_LAYER_ALE_AUTH_CONNECT_V6,
    &FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, &FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6,
    &FWPM_LAYER_ALE_AUTH_LISTEN_V4, &FWPM_LAYER_ALE_AUTH_LISTEN_V6,
};

static const struct { const GUID *g; unsigned short field; } g_fields[] = {
    { &FWPM_CONDITION_IP_PROTOCOL, NOVAWFP_F_PROTOCOL },
    { &FWPM_CONDITION_IP_LOCAL_ADDRESS, NOVAWFP_F_LOCAL_ADDR },
    { &FWPM_CONDITION_IP_REMOTE_ADDRESS, NOVAWFP_F_REMOTE_ADDR },
    { &FWPM_CONDITION_IP_LOCAL_PORT, NOVAWFP_F_LOCAL_PORT },
    { &FWPM_CONDITION_IP_REMOTE_PORT, NOVAWFP_F_REMOTE_PORT },
    { &FWPM_CONDITION_ALE_APP_ID, NOVAWFP_F_APP_ID },
    { &FWPM_CONDITION_FLAGS, NOVAWFP_F_FLAGS },
    { &FWPM_CONDITION_INTERFACE_INDEX, NOVAWFP_F_IF_INDEX },
    { &FWPM_CONDITION_IP_LOCAL_INTERFACE, NOVAWFP_F_IF_LUID },
};

static int layer_index(const GUID *g)
{
    for (int i = 0; i < NOVAWFP_LAYERS; i++) if (!memcmp(g, g_layers[i], sizeof(GUID))) return i;
    return -1;
}

/* The engine's answer as these functions return it: FWP_E_* stay, the
 * generic HRESULTs become Win32 errors */
static DWORD answer(LONG_PTR r)
{
    DWORD hr = (DWORD)r;
    switch (hr) {
    case 0:           return ERROR_SUCCESS;
    case 0x80070005u: return ERROR_ACCESS_DENIED;
    case 0x8007000Eu: return ERROR_NOT_ENOUGH_MEMORY;
    case 0x80070006u: return ERROR_INVALID_HANDLE;
    case 0x80004003u: return ERROR_INVALID_PARAMETER;
    default:          return hr;
    }
}

static DWORD ctl(UINT32 session, UINT32 op, void *req)
{
    return answer(NtNovaSockCtl(0, NOVAFW_WFP, (ULONG_PTR)(op | (session << 16)), req));
}

static Engine *engine_of(HANDLE h)
{
    Engine *e = h;
    return e && e->magic == ENGINE_MAGIC ? e : NULL;
}

static void wide_to_utf8(const wchar_t *w, char *out, int cap)
{
    int n = 0;
    if (w) for (; *w && n < cap - 1; w++) {
        UINT32 c = *w;
        if (c < 0x80) out[n++] = (char)c;
        else if (c < 0x800) { if (n + 2 >= cap) break; out[n++] = (char)(0xC0 | (c >> 6)); out[n++] = (char)(0x80 | (c & 0x3F)); }
        else { if (n + 3 >= cap) break; out[n++] = (char)(0xE0 | (c >> 12)); out[n++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[n++] = (char)(0x80 | (c & 0x3F)); }
    }
    out[n] = 0;
}

static wchar_t *utf8_to_wide_dup(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    wchar_t *w = zalloc((SIZE_T)(n + 1) * sizeof(wchar_t));
    if (!w) return NULL;
    int k = 0;
    for (int i = 0; i < n;) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { w[k++] = c; i++; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < n) { w[k++] = (wchar_t)(((c & 0x1F) << 6) | (s[i + 1] & 0x3F)); i += 2; }
        else if (i + 2 < n) { w[k++] = (wchar_t)(((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F)); i += 3; }
        else i++;
    }
    return w;
}

/* ---------------------------------------------------------------------------
 * Sessions and transactions
 * ------------------------------------------------------------------------- */
FWPAPI DWORD WINAPI FwpmEngineOpen0(const wchar_t *serverName, UINT32 authnService, void *authIdentity,
                                    const FWPM_SESSION0 *session, HANDLE *engineHandle)
{
    (void)authIdentity;
    if (!engineHandle) return ERROR_INVALID_PARAMETER;
    if (serverName && serverName[0]) return ERROR_BAD_NETPATH;      /* (a remote engine: not here) */
    if (authnService != RPC_C_AUTHN_WINNT && authnService != (UINT32)RPC_C_AUTHN_DEFAULT) return ERROR_INVALID_PARAMETER;
    UINT32 req = session ? session->flags : 0;
    DWORD r = ctl(0, NOVAWFP_OPEN, &req);
    if (r) return r;
    Engine *e = zalloc(sizeof(*e));
    if (!e) { ctl(req, NOVAWFP_CLOSE, NULL); return ERROR_NOT_ENOUGH_MEMORY; }
    e->magic = ENGINE_MAGIC;
    e->session = req;
    *engineHandle = e;
    return ERROR_SUCCESS;
}

FWPAPI DWORD WINAPI FwpmEngineClose0(HANDLE engineHandle)
{
    Engine *e = engine_of(engineHandle);
    if (!e) return ERROR_INVALID_HANDLE;
    DWORD r = ctl(e->session, NOVAWFP_CLOSE, NULL);
    e->magic = 0;
    zfree(e);
    return r;
}

FWPAPI DWORD WINAPI FwpmEngineGetOption0(HANDLE engineHandle, int option, FWP_VALUE0 **value)
{
    (void)option;
    if (!engine_of(engineHandle) || !value) return ERROR_INVALID_PARAMETER;
    FWP_VALUE0 *v = zalloc(sizeof(*v));
    if (!v) return ERROR_NOT_ENOUGH_MEMORY;
    v->type = FWP_UINT32;                        /* (net events, packet queuing...: off) */
    *value = v;
    return ERROR_SUCCESS;
}

FWPAPI DWORD WINAPI FwpmEngineSetOption0(HANDLE engineHandle, int option, const FWP_VALUE0 *value)
{
    (void)option; (void)value;
    return engine_of(engineHandle) ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

FWPAPI DWORD WINAPI FwpmTransactionBegin0(HANDLE engineHandle, UINT32 flags)
{
    (void)flags;
    Engine *e = engine_of(engineHandle);
    return e ? ctl(e->session, NOVAWFP_TXN_BEGIN, NULL) : ERROR_INVALID_HANDLE;
}

FWPAPI DWORD WINAPI FwpmTransactionCommit0(HANDLE engineHandle)
{
    Engine *e = engine_of(engineHandle);
    return e ? ctl(e->session, NOVAWFP_TXN_COMMIT, NULL) : ERROR_INVALID_HANDLE;
}

FWPAPI DWORD WINAPI FwpmTransactionAbort0(HANDLE engineHandle)
{
    Engine *e = engine_of(engineHandle);
    return e ? ctl(e->session, NOVAWFP_TXN_ABORT, NULL) : ERROR_INVALID_HANDLE;
}

/* ---------------------------------------------------------------------------
 * Filters
 * ------------------------------------------------------------------------- */
static BOOL number_of(const FWP_CONDITION_VALUE0 *v, unsigned long long *out)
{
    switch (v->type) {
    case FWP_UINT8:  *out = v->uint8; return TRUE;
    case FWP_UINT16: *out = v->uint16; return TRUE;
    case FWP_UINT32: *out = v->uint32; return TRUE;
    case FWP_UINT64: if (!v->uint64) return FALSE; *out = *v->uint64; return TRUE;
    default:         return FALSE;
    }
}

static BOOL value_number(const FWP_VALUE0 *v, unsigned long long *out)
{
    FWP_CONDITION_VALUE0 c;
    memset(&c, 0, sizeof(c));
    c.type = v->type;
    memcpy(&c.uint64, &v->uint64, sizeof(void *));
    return number_of(&c, out);
}

static DWORD condition_in(const FWPM_FILTER_CONDITION0 *in, NovaWfpCond *c)
{
    memset(c, 0, sizeof(*c));
    for (unsigned i = 0; i < sizeof g_fields / sizeof g_fields[0]; i++)
        if (!memcmp(&in->fieldKey, g_fields[i].g, sizeof(GUID))) c->field = g_fields[i].field;
    if (!c->field) return FWP_E_INVALID_PARAMETER;           /* (a condition this engine does not have) */
    if ((unsigned)in->matchType >= FWP_MATCH_TYPE_MAX) return FWP_E_INVALID_PARAMETER;
    c->match = (unsigned char)in->matchType;
    const FWP_CONDITION_VALUE0 *v = &in->conditionValue;
    switch (v->type) {
    case FWP_V4_ADDR_MASK: {
        if (!v->v4AddrMask) return ERROR_INVALID_PARAMETER;
        UINT32 a = v->v4AddrMask->addr, m = v->v4AddrMask->mask;
        c->kind = NOVAWFP_V_ADDR4;
        c->addr[0] = (unsigned char)(a >> 24); c->addr[1] = (unsigned char)(a >> 16);
        c->addr[2] = (unsigned char)(a >> 8);  c->addr[3] = (unsigned char)a;
        int bits = 0;
        while (bits < 32 && (m & (0x80000000u >> bits))) bits++;
        c->prefix = (unsigned char)bits;
        return 0;
    }
    case FWP_V6_ADDR_MASK:
        if (!v->v6AddrMask) return ERROR_INVALID_PARAMETER;
        c->kind = NOVAWFP_V_ADDR6;
        memcpy(c->addr, v->v6AddrMask->addr, 16);
        c->prefix = v->v6AddrMask->prefixLength > 128 ? 128 : v->v6AddrMask->prefixLength;
        return 0;
    case FWP_BYTE_ARRAY16_TYPE:                              /* an IPv6 address */
        if (!v->byteArray16) return ERROR_INVALID_PARAMETER;
        c->kind = NOVAWFP_V_ADDR6;
        memcpy(c->addr, v->byteArray16->byteArray16, 16);
        c->prefix = 128;
        return 0;
    case FWP_BYTE_BLOB_TYPE: {                               /* the application id: a NUL-terminated UTF-16 path */
        if (!v->byteBlob || !v->byteBlob->data) return ERROR_INVALID_PARAMETER;
        wchar_t w[NOVAWFP_APP_MAX];
        UINT32 n = v->byteBlob->size / 2;
        if (n >= NOVAWFP_APP_MAX) n = NOVAWFP_APP_MAX - 1;
        memcpy(w, v->byteBlob->data, n * 2);
        w[n] = 0;
        c->kind = NOVAWFP_V_APP;
        wide_to_utf8(w, c->app, sizeof(c->app));
        return 0;
    }
    case FWP_RANGE_TYPE:
        if (!v->rangeValue || !value_number(&v->rangeValue->valueLow, &c->lo) ||
            !value_number(&v->rangeValue->valueHigh, &c->hi)) return FWP_E_INVALID_PARAMETER;
        c->kind = NOVAWFP_V_RANGE;
        c->match = NOVAWFP_MATCH_RANGE;
        return 0;
    default:
        if (!number_of(v, &c->lo)) return FWP_E_INVALID_PARAMETER;
        c->kind = NOVAWFP_V_NUM;
        c->hi = c->lo;
        return 0;
    }
}

FWPAPI DWORD WINAPI FwpmFilterAdd0(HANDLE engineHandle, const FWPM_FILTER0 *filter, void *sd, UINT64 *id)
{
    (void)sd;
    Engine *e = engine_of(engineHandle);
    if (!e) return ERROR_INVALID_HANDLE;
    if (!filter) return ERROR_INVALID_PARAMETER;
    int layer = layer_index(&filter->layerKey);
    if (layer < 0) return FWP_E_LAYER_NOT_FOUND;
    if (filter->numFilterConditions > NOVAWFP_MAX_COND || (filter->numFilterConditions && !filter->filterCondition))
        return FWP_E_INVALID_PARAMETER;
    NovaWfpFilter *f = zalloc(sizeof(*f));
    if (!f) return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(&f->key, &filter->filterKey, sizeof(GUID));
    memcpy(&f->sublayer, &filter->subLayerKey, sizeof(GUID));
    if (filter->providerKey) { f->has_provider = 1; memcpy(&f->provider, filter->providerKey, sizeof(GUID)); }
    f->layer = (unsigned)layer;
    f->action = filter->action.type;
    f->flags = filter->flags;
    f->weight_type = filter->weight.type;
    if (filter->weight.type == FWP_UINT8) f->weight = filter->weight.uint8;
    else if (filter->weight.type == FWP_UINT64) f->weight = filter->weight.uint64 ? *filter->weight.uint64 : 0;
    else if (filter->weight.type != FWP_EMPTY) { zfree(f); return FWP_E_INVALID_WEIGHT; }
    wide_to_utf8(filter->displayData.name, f->name, sizeof(f->name));
    wide_to_utf8(filter->displayData.description, f->desc, sizeof(f->desc));
    f->ncond = filter->numFilterConditions;
    for (UINT32 i = 0; i < f->ncond; i++) {
        DWORD r = condition_in(&filter->filterCondition[i], &f->cond[i]);
        if (r) { zfree(f); return r; }
    }
    DWORD r = ctl(e->session, NOVAWFP_FILTER_ADD, f);
    if (!r && id) *id = f->id;
    zfree(f);
    return r;
}

FWPAPI DWORD WINAPI FwpmFilterDeleteById0(HANDLE engineHandle, UINT64 id)
{
    Engine *e = engine_of(engineHandle);
    if (!e) return ERROR_INVALID_HANDLE;
    unsigned long long v = id;
    return ctl(e->session, NOVAWFP_FILTER_DELETE_ID, &v);
}

FWPAPI DWORD WINAPI FwpmFilterDeleteByKey0(HANDLE engineHandle, const GUID *key)
{
    Engine *e = engine_of(engineHandle);
    if (!e) return ERROR_INVALID_HANDLE;
    if (!key) return ERROR_INVALID_PARAMETER;
    NovaGuid k;
    memcpy(&k, key, sizeof(k));
    return ctl(e->session, NOVAWFP_FILTER_DELETE_KEY, &k);
}

/* A filter as Windows hands it out: one allocation (FwpmFreeMemory0 frees
 * it) holding the structure, its conditions, their values and its names */
typedef struct {
    FWPM_FILTER0            f;
    FWPM_FILTER_CONDITION0  c[NOVAWFP_MAX_COND];
    union { FWP_V4_ADDR_AND_MASK v4; FWP_V6_ADDR_AND_MASK v6; FWP_RANGE0 range; FWP_BYTE_BLOB blob; } val[NOVAWFP_MAX_COND];
    UINT64                  u64[NOVAWFP_MAX_COND];
    GUID                    provider;
    UINT64                  weight, eff;
    wchar_t                 name[NOVAWFP_NAME_MAX], desc[96], app[NOVAWFP_MAX_COND][NOVAWFP_APP_MAX];
} FilterOut;

static void put_wide(wchar_t *dst, int cap, const char *s)
{
    int i = 0;
    for (; s[i] && i < cap - 1; i++) dst[i] = (unsigned char)s[i];
    dst[i] = 0;
}

static FWPM_FILTER0 *filter_out(const NovaWfpFilter *in)
{
    FilterOut *o = zalloc(sizeof(*o));
    if (!o) return NULL;
    FWPM_FILTER0 *f = &o->f;
    memcpy(&f->filterKey, &in->key, sizeof(GUID));
    memcpy(&f->subLayerKey, &in->sublayer, sizeof(GUID));
    if (in->layer < NOVAWFP_LAYERS) memcpy(&f->layerKey, g_layers[in->layer], sizeof(GUID));
    if (in->has_provider) { memcpy(&o->provider, &in->provider, sizeof(GUID)); f->providerKey = &o->provider; }
    put_wide(o->name, NOVAWFP_NAME_MAX, in->name);
    put_wide(o->desc, 96, in->desc);
    f->displayData.name = o->name;
    f->displayData.description = o->desc;
    f->flags = in->flags;
    f->action.type = in->action;
    f->filterId = in->id;
    f->weight.type = in->weight_type == 1 ? FWP_UINT8 : in->weight_type == 4 ? FWP_UINT64 : FWP_EMPTY;
    if (in->weight_type == 1) f->weight.uint8 = (UINT8)(in->weight >> 60);
    o->weight = in->weight;
    if (in->weight_type == 4) f->weight.uint64 = &o->weight;
    o->eff = in->weight;
    f->effectiveWeight.type = FWP_UINT64;
    f->effectiveWeight.uint64 = &o->eff;
    f->numFilterConditions = in->ncond;
    f->filterCondition = in->ncond ? o->c : NULL;
    for (UINT32 i = 0; i < in->ncond && i < NOVAWFP_MAX_COND; i++) {
        const NovaWfpCond *c = &in->cond[i];
        FWPM_FILTER_CONDITION0 *d = &o->c[i];
        memcpy(&d->fieldKey, g_fields[0].g, sizeof(GUID));
        for (unsigned k = 0; k < sizeof g_fields / sizeof g_fields[0]; k++)
            if (g_fields[k].field == c->field) memcpy(&d->fieldKey, g_fields[k].g, sizeof(GUID));
        d->matchType = (FWP_MATCH_TYPE)c->match;
        FWP_CONDITION_VALUE0 *v = &d->conditionValue;
        switch (c->kind) {
        case NOVAWFP_V_ADDR4:
            o->val[i].v4.addr = (UINT32)c->addr[0] << 24 | (UINT32)c->addr[1] << 16 | (UINT32)c->addr[2] << 8 | c->addr[3];
            o->val[i].v4.mask = c->prefix ? 0xFFFFFFFFu << (32 - c->prefix) : 0;
            v->type = FWP_V4_ADDR_MASK; v->v4AddrMask = &o->val[i].v4;
            break;
        case NOVAWFP_V_ADDR6:
            memcpy(o->val[i].v6.addr, c->addr, 16);
            o->val[i].v6.prefixLength = c->prefix;
            v->type = FWP_V6_ADDR_MASK; v->v6AddrMask = &o->val[i].v6;
            break;
        case NOVAWFP_V_APP: {
            put_wide(o->app[i], NOVAWFP_APP_MAX, c->app);
            int n = 0;
            while (o->app[i][n]) n++;
            o->val[i].blob.size = (UINT32)(n + 1) * 2;
            o->val[i].blob.data = (UINT8 *)o->app[i];
            v->type = FWP_BYTE_BLOB_TYPE; v->byteBlob = &o->val[i].blob;
            break;
        }
        case NOVAWFP_V_RANGE:
            o->val[i].range.valueLow.type = o->val[i].range.valueHigh.type = FWP_UINT16;
            o->val[i].range.valueLow.uint16 = (UINT16)c->lo;
            o->val[i].range.valueHigh.uint16 = (UINT16)c->hi;
            v->type = FWP_RANGE_TYPE; v->rangeValue = &o->val[i].range;
            break;
        default:
            if (c->field == NOVAWFP_F_PROTOCOL) { v->type = FWP_UINT8; v->uint8 = (UINT8)c->lo; }
            else if (c->field == NOVAWFP_F_LOCAL_PORT || c->field == NOVAWFP_F_REMOTE_PORT) { v->type = FWP_UINT16; v->uint16 = (UINT16)c->lo; }
            else if (c->field == NOVAWFP_F_IF_LUID) { o->u64[i] = c->lo; v->type = FWP_UINT64; v->uint64 = &o->u64[i]; }
            else { v->type = FWP_UINT32; v->uint32 = (UINT32)c->lo; }
            break;
        }
    }
    return f;
}

FWPAPI DWORD WINAPI FwpmFilterGetById0(HANDLE engineHandle, UINT64 id, FWPM_FILTER0 **filter)
{
    Engine *e = engine_of(engineHandle);
    if (!e) return ERROR_INVALID_HANDLE;
    if (!filter) return ERROR_INVALID_PARAMETER;
    NovaWfpFilter *f = zalloc(sizeof(*f));
    if (!f) return ERROR_NOT_ENOUGH_MEMORY;
    f->id = id;
    DWORD r = ctl(e->session, NOVAWFP_FILTER_GET_ID, f);
    if (!r && !(*filter = filter_out(f))) r = ERROR_NOT_ENOUGH_MEMORY;
    zfree(f);
    return r;
}

FWPAPI DWORD WINAPI FwpmFilterGetByKey0(HANDLE engineHandle, const GUID *key, FWPM_FILTER0 **filter)
{
    Engine *e = engine_of(engineHandle);
    if (!e) return ERROR_INVALID_HANDLE;
    if (!filter || !key) return ERROR_INVALID_PARAMETER;
    NovaWfpFilter *f = zalloc(sizeof(*f));
    if (!f) return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(&f->key, key, sizeof(GUID));
    DWORD r = ctl(e->session, NOVAWFP_FILTER_GET_KEY, f);
    if (!r && !(*filter = filter_out(f))) r = ERROR_NOT_ENOUGH_MEMORY;
    zfree(f);
    return r;
}

FWPAPI DWORD WINAPI FwpmFilterCreateEnumHandle0(HANDLE engineHandle, const FWPM_FILTER_ENUM_TEMPLATE0 *enumTemplate, HANDLE *enumHandle)
{
    if (!engine_of(engineHandle)) return ERROR_INVALID_HANDLE;
    if (!enumHandle) return ERROR_INVALID_PARAMETER;
    EnumHandle *h = zalloc(sizeof(*h));
    if (!h) return ERROR_NOT_ENOUGH_MEMORY;
    h->magic = ENUM_MAGIC;
    h->any_layer = TRUE;
    if (enumTemplate) {
        h->layer = layer_index(&enumTemplate->layerKey);
        h->any_layer = FALSE;
        if (h->layer < 0) { zfree(h); return FWP_E_LAYER_NOT_FOUND; }
        if (enumTemplate->providerKey) { h->by_provider = TRUE; memcpy(&h->provider, enumTemplate->providerKey, sizeof(GUID)); }
    }
    *enumHandle = h;
    return ERROR_SUCCESS;
}

FWPAPI DWORD WINAPI FwpmFilterEnum0(HANDLE engineHandle, HANDLE enumHandle, UINT32 numEntriesRequested,
                                    FWPM_FILTER0 ***entries, UINT32 *numEntriesReturned)
{
    Engine *e = engine_of(engineHandle);
    EnumHandle *h = enumHandle;
    if (!e) return ERROR_INVALID_HANDLE;
    if (!h || h->magic != ENUM_MAGIC || h->subl || !entries || !numEntriesReturned) return ERROR_INVALID_PARAMETER;
    *entries = NULL;
    *numEntriesReturned = 0;
    if (!numEntriesRequested) return ERROR_SUCCESS;
    if (numEntriesRequested > 1024) numEntriesRequested = 1024;
    FWPM_FILTER0 **list = zalloc(numEntriesRequested * sizeof(*list));
    NovaWfpFilter *f = zalloc(sizeof(*f));
    if (!list || !f) { zfree(list); zfree(f); return ERROR_NOT_ENOUGH_MEMORY; }
    UINT32 n = 0;
    while (n < numEntriesRequested) {
        memset(f, 0, sizeof(*f));
        f->id = h->next;
        if (ctl(e->session, NOVAWFP_FILTER_AT, f)) break;
        h->next++;
        if (!h->any_layer && (int)f->layer != h->layer) continue;
        if (h->by_provider && (!f->has_provider || memcmp(&f->provider, &h->provider, sizeof(GUID)))) continue;
        if (!(list[n] = filter_out(f))) break;
        n++;
    }
    zfree(f);
    if (!n) { zfree(list); return ERROR_SUCCESS; }
    *entries = list;
    *numEntriesReturned = n;
    return ERROR_SUCCESS;
}

FWPAPI DWORD WINAPI FwpmFilterDestroyEnumHandle0(HANDLE engineHandle, HANDLE enumHandle)
{
    EnumHandle *h = enumHandle;
    if (!engine_of(engineHandle)) return ERROR_INVALID_HANDLE;
    if (!h || h->magic != ENUM_MAGIC) return ERROR_INVALID_PARAMETER;
    h->magic = 0;
    zfree(h);
    return ERROR_SUCCESS;
}

/* ---------------------------------------------------------------------------
 * Sublayers and providers
 * ------------------------------------------------------------------------- */
typedef struct { FWPM_SUBLAYER0 s; GUID provider; wchar_t name[NOVAWFP_NAME_MAX]; } SublayerOut;
typedef struct { FWPM_PROVIDER0 p; wchar_t name[NOVAWFP_NAME_MAX]; } ProviderOut;

static FWPM_SUBLAYER0 *sublayer_out(const NovaWfpSublayer *in)
{
    SublayerOut *o = zalloc(sizeof(*o));
    if (!o) return NULL;
    memcpy(&o->s.subLayerKey, &in->key, sizeof(GUID));
    put_wide(o->name, NOVAWFP_NAME_MAX, in->name);
    o->s.displayData.name = o->name;
    o->s.flags = (UINT16)in->flags;
    o->s.weight = (UINT16)in->weight;
    if (in->has_provider) { memcpy(&o->provider, &in->provider, sizeof(GUID)); o->s.providerKey = &o->provider; }
    return &o->s;
}

FWPAPI DWORD WINAPI FwpmSubLayerAdd0(HANDLE engineHandle, const FWPM_SUBLAYER0 *subLayer, void *sd)
{
    (void)sd;
    Engine *e = engine_of(engineHandle);
    if (!e) return ERROR_INVALID_HANDLE;
    if (!subLayer) return ERROR_INVALID_PARAMETER;
    NovaWfpSublayer s;
    memset(&s, 0, sizeof(s));
    memcpy(&s.key, &subLayer->subLayerKey, sizeof(GUID));
    if (subLayer->providerKey) { s.has_provider = 1; memcpy(&s.provider, subLayer->providerKey, sizeof(GUID)); }
    s.flags = subLayer->flags;
    s.weight = subLayer->weight;
    wide_to_utf8(subLayer->displayData.name, s.name, sizeof(s.name));
    return ctl(e->session, NOVAWFP_SUBLAYER_ADD, &s);
}

FWPAPI DWORD WINAPI FwpmSubLayerDeleteByKey0(HANDLE engineHandle, const GUID *key)
{
    Engine *e = engine_of(engineHandle);
    if (!e) return ERROR_INVALID_HANDLE;
    if (!key) return ERROR_INVALID_PARAMETER;
    NovaGuid k;
    memcpy(&k, key, sizeof(k));
    return ctl(e->session, NOVAWFP_SUBLAYER_DELETE, &k);
}

FWPAPI DWORD WINAPI FwpmSubLayerGetByKey0(HANDLE engineHandle, const GUID *key, FWPM_SUBLAYER0 **subLayer)
{
    Engine *e = engine_of(engineHandle);
    if (!e) return ERROR_INVALID_HANDLE;
    if (!key || !subLayer) return ERROR_INVALID_PARAMETER;
    NovaWfpSublayer s;
    memset(&s, 0, sizeof(s));
    memcpy(&s.key, key, sizeof(GUID));
    DWORD r = ctl(e->session, NOVAWFP_SUBLAYER_GET, &s);
    if (!r && !(*subLayer = sublayer_out(&s))) r = ERROR_NOT_ENOUGH_MEMORY;
    return r;
}

FWPAPI DWORD WINAPI FwpmSubLayerCreateEnumHandle0(HANDLE engineHandle, const FWPM_SUBLAYER_ENUM_TEMPLATE0 *enumTemplate, HANDLE *enumHandle)
{
    if (!engine_of(engineHandle)) return ERROR_INVALID_HANDLE;
    if (!enumHandle) return ERROR_INVALID_PARAMETER;
    EnumHandle *h = zalloc(sizeof(*h));
    if (!h) return ERROR_NOT_ENOUGH_MEMORY;
    h->magic = ENUM_MAGIC;
    h->subl = TRUE;
    if (enumTemplate && enumTemplate->providerKey) { h->by_provider = TRUE; memcpy(&h->provider, enumTemplate->providerKey, sizeof(GUID)); }
    *enumHandle = h;
    return ERROR_SUCCESS;
}

FWPAPI DWORD WINAPI FwpmSubLayerEnum0(HANDLE engineHandle, HANDLE enumHandle, UINT32 numEntriesRequested,
                                      FWPM_SUBLAYER0 ***entries, UINT32 *numEntriesReturned)
{
    Engine *e = engine_of(engineHandle);
    EnumHandle *h = enumHandle;
    if (!e) return ERROR_INVALID_HANDLE;
    if (!h || h->magic != ENUM_MAGIC || !h->subl || !entries || !numEntriesReturned) return ERROR_INVALID_PARAMETER;
    *entries = NULL;
    *numEntriesReturned = 0;
    if (!numEntriesRequested) return ERROR_SUCCESS;
    if (numEntriesRequested > 256) numEntriesRequested = 256;
    FWPM_SUBLAYER0 **list = zalloc(numEntriesRequested * sizeof(*list));
    if (!list) return ERROR_NOT_ENOUGH_MEMORY;
    UINT32 n = 0;
    while (n < numEntriesRequested) {
        NovaWfpSublayer s;
        memset(&s, 0, sizeof(s));
        s.index = h->next;
        if (ctl(e->session, NOVAWFP_SUBLAYER_AT, &s)) break;
        h->next++;
        if (h->by_provider && (!s.has_provider || memcmp(&s.provider, &h->provider, sizeof(GUID)))) continue;
        if (!(list[n] = sublayer_out(&s))) break;
        n++;
    }
    if (!n) { zfree(list); return ERROR_SUCCESS; }
    *entries = list;
    *numEntriesReturned = n;
    return ERROR_SUCCESS;
}

FWPAPI DWORD WINAPI FwpmSubLayerDestroyEnumHandle0(HANDLE engineHandle, HANDLE enumHandle)
{
    return FwpmFilterDestroyEnumHandle0(engineHandle, enumHandle);
}

FWPAPI DWORD WINAPI FwpmProviderAdd0(HANDLE engineHandle, const FWPM_PROVIDER0 *provider, void *sd)
{
    (void)sd;
    Engine *e = engine_of(engineHandle);
    if (!e) return ERROR_INVALID_HANDLE;
    if (!provider) return ERROR_INVALID_PARAMETER;
    NovaWfpProvider p;
    memset(&p, 0, sizeof(p));
    memcpy(&p.key, &provider->providerKey, sizeof(GUID));
    p.flags = provider->flags;
    wide_to_utf8(provider->displayData.name, p.name, sizeof(p.name));
    return ctl(e->session, NOVAWFP_PROVIDER_ADD, &p);
}

FWPAPI DWORD WINAPI FwpmProviderDeleteByKey0(HANDLE engineHandle, const GUID *key)
{
    Engine *e = engine_of(engineHandle);
    if (!e) return ERROR_INVALID_HANDLE;
    if (!key) return ERROR_INVALID_PARAMETER;
    NovaGuid k;
    memcpy(&k, key, sizeof(k));
    return ctl(e->session, NOVAWFP_PROVIDER_DELETE, &k);
}

FWPAPI DWORD WINAPI FwpmProviderGetByKey0(HANDLE engineHandle, const GUID *key, FWPM_PROVIDER0 **provider)
{
    Engine *e = engine_of(engineHandle);
    if (!e) return ERROR_INVALID_HANDLE;
    if (!key || !provider) return ERROR_INVALID_PARAMETER;
    NovaWfpProvider p;
    memset(&p, 0, sizeof(p));
    memcpy(&p.key, key, sizeof(GUID));
    DWORD r = ctl(e->session, NOVAWFP_PROVIDER_GET, &p);
    if (r) return r;
    ProviderOut *o = zalloc(sizeof(*o));
    if (!o) return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(&o->p.providerKey, &p.key, sizeof(GUID));
    put_wide(o->name, NOVAWFP_NAME_MAX, p.name);
    o->p.displayData.name = o->name;
    o->p.flags = p.flags;
    *provider = &o->p;
    return ERROR_SUCCESS;
}

/* ---------------------------------------------------------------------------
 * Application ids and memory
 * ------------------------------------------------------------------------- */
/* The program's path as the engine names it: "\device\harddiskvolumeN\..."
 * in lower case (QueryDosDevice gives the drive's device), NUL included */
FWPAPI DWORD WINAPI FwpmGetAppIdFromFileName0(const wchar_t *fileName, FWP_BYTE_BLOB **appId)
{
    if (!fileName || !appId) return ERROR_INVALID_PARAMETER;
    wchar_t full[MAX_PATH], dev[MAX_PATH];
    DWORD n = GetFullPathNameW(fileName, MAX_PATH, full, NULL);
    if (!n || n >= MAX_PATH) return ERROR_INVALID_PARAMETER;
    if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) return ERROR_FILE_NOT_FOUND;
    wchar_t drive[3] = { full[0], L':', 0 };
    int dn = 0;
    if (full[1] == L':' && QueryDosDeviceW(drive, dev, MAX_PATH)) while (dev[dn]) dn++;
    else { const wchar_t *d = L"\\device\\harddiskvolume1"; while (d[dn]) { dev[dn] = d[dn]; dn++; } }
    const wchar_t *rest = full[1] == L':' ? full + 2 : full;
    int rn = 0;
    while (rest[rn]) rn++;
    FWP_BYTE_BLOB *b = zalloc(sizeof(*b) + (SIZE_T)(dn + rn + 1) * sizeof(wchar_t));
    if (!b) return ERROR_NOT_ENOUGH_MEMORY;
    wchar_t *w = (wchar_t *)(b + 1);
    for (int i = 0; i < dn; i++) w[i] = dev[i];
    for (int i = 0; i < rn; i++) w[dn + i] = rest[i];
    for (int i = 0; i < dn + rn; i++) if (w[i] >= L'A' && w[i] <= L'Z') w[i] = (wchar_t)(w[i] + 32);
    b->size = (UINT32)(dn + rn + 1) * sizeof(wchar_t);
    b->data = (UINT8 *)w;
    *appId = b;
    return ERROR_SUCCESS;
}

/* Frees what the Get, Enum and AppId calls handed out (one allocation
 * each; an Enum's array of them goes with the array) and sets *p NULL */
FWPAPI void WINAPI FwpmFreeMemory0(void **p)
{
    if (!p || !*p) return;
    zfree(*p);
    *p = NULL;
}
