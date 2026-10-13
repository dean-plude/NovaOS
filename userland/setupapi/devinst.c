/*
 * setupapi.dll — installing a staged driver package on a device
 *
 * What Windows' device installer does once a package is in the driver
 * store (driver.c) and a device's hardware or compatible ID matches one of
 * its models: the model's install section (DDInstall, decorated .NTamd64
 * or .NT when the INF has one) is carried out for that device instance.
 *
 *   CopyFiles      each file to its DestinationDirs folder (dirid 12,
 *                  System32\drivers, or 13: run from the driver store, no copy)
 *   .Services      AddService: the service key (CreateService); the one
 *                  flagged SPSVCINST_ASSOCSERVICE (2) runs the device
 *   AddReg         HKR lines into the device's driver key (an NDIS
 *                  miniport's parameters: NdisReadConfiguration reads them)
 *   .HW AddReg     HKR lines into the device's Device Parameters key
 *   .Wdf           a UMDF driver's services: UmdfService = name, section
 *                  (the section's ServiceBinary and UmdfLibraryVersion,
 *                  under Services\WUDF\Services\<name>), UmdfServiceOrder
 *                  (the device's Device Parameters) and the Umdf* policy
 *                  directives (Device Parameters\WUDF), which the UMDF
 *                  driver host (tools/umdf/host) reads
 *
 * and the device's keys are written as Windows writes them:
 * HKLM\SYSTEM\CurrentControlSet\Enum\<instance> (Service, ClassGUID, Class,
 * Driver, DeviceDesc, Mfg, ConfigFlags) and its driver key
 * Control\Class\{class GUID}\NNNN (InfPath, InfSection, MatchingDeviceId,
 * DriverDesc, ProviderName, DriverVersion, DriverDate).  The kernel's PnP
 * manager (kernel/nt/pnp.c) starts the driver from them: at once when newdev
 * asks, and at every boot after.
 */
#include "inf.h"
#include <winsvc.h>

#ifndef ERROR_SERVICE_EXISTS
#define ERROR_SERVICE_EXISTS 1073
#endif
/* (advapi32's; NovaOS's winsvc.h does not declare it) */
__declspec(dllimport) BOOL WINAPI ChangeServiceConfigW(SC_HANDLE h, DWORD type, DWORD start, DWORD errc, LPCWSTR path,
                                                       LPCWSTR group, LPDWORD tag, LPCWSTR deps, LPCWSTR user,
                                                       LPCWSTR password, LPCWSTR display);

#define SETUPAPI __declspec(dllexport)
#define ENUM_KEY  L"SYSTEM\\CurrentControlSet\\Enum\\"
#define CLASS_KEY L"SYSTEM\\CurrentControlSet\\Control\\Class\\"
#define DB_KEY    L"SYSTEM\\DriverDatabase\\DriverInfFiles\\"
#define SPSVCINST_ASSOCSERVICE 0x00000002

typedef struct {
    const Inf *inf;
    const WCHAR *hwid;                          /* the ID to match */
    WCHAR models[128];                          /* the models section being read */
    WCHAR section[128], desc[256];              /* what matched: install section, model description */
    BOOL  found;
} Match;

static BOOL ieq_w(const WCHAR *a, const WCHAR *b) { return !lstrcmpiW(a, b); }

/* (setupapi links no C runtime) */
static BOOL prefix_w(const WCHAR *s, const WCHAR *pre)
{
    for (; *pre; s++, pre++) {
        WCHAR a = *s >= 'A' && *s <= 'Z' ? *s + 32 : *s, b = *pre >= 'A' && *pre <= 'Z' ? *pre + 32 : *pre;
        if (a != b) return FALSE;
    }
    return TRUE;
}

/* A decimal or 0x hexadecimal number */
static DWORD num_w(const WCHAR *s)
{
    DWORD v = 0;
    while (*s == ' ') s++;
    if (s[0] == '0' && (s[1] | 0x20) == 'x') {
        for (s += 2;; s++) {
            int d = *s >= '0' && *s <= '9' ? *s - '0' : (*s | 0x20) >= 'a' && (*s | 0x20) <= 'f' ? (*s | 0x20) - 'a' + 10 : -1;
            if (d < 0) return v;
            v = v * 16 + (DWORD)d;
        }
    }
    while (*s >= '0' && *s <= '9') v = v * 10 + (DWORD)(*s++ - '0');
    return v;
}

static void windir_w(WCHAR *out, const WCHAR *rest)
{
    UINT n = GetWindowsDirectoryW(out, MAX_PATH);
    if (!n || n >= MAX_PATH) lstrcpyW(out, L"C:\\Windows");
    lstrcatW(out, rest);
}

/* Whether section @name has any line */
static BOOL any_line(const WCHAR *k, const WCHAR *v, void *ctx) { (void)k; (void)v; *(BOOL *)ctx = TRUE; return FALSE; }
static BOOL has_section(const Inf *inf, const WCHAR *name)
{
    BOOL yes = FALSE;
    inf_lines(inf, name, any_line, &yes);
    return yes;
}

/* A models section's line: "desc = section, id, id..." */
static BOOL model_line(const WCHAR *key, const WCHAR *value, void *ctx)
{
    Match *m = ctx;
    WCHAR f[256];
    for (int i = 1; i < 16; i++) {
        inf_field(value, i, f, 256);
        if (!f[0]) break;
        if (ieq_w(f, m->hwid)) {
            inf_field(value, 0, m->section, 128);
            lstrcpynW(m->desc, key, 256);
            int n = lstrlenW(key);                          /* (a %strkey% description, from [Strings]) */
            if (n > 2 && key[0] == '%' && key[n - 1] == '%' && n < 130) {
                WCHAR name[128];
                lstrcpynW(name, key + 1, n - 1);
                if (!inf_get(m->inf, L"Strings", name, m->desc, 256)) lstrcpynW(m->desc, key, 256);
            }
            m->found = TRUE;
            return FALSE;
        }
    }
    return TRUE;
}

/* A [Manufacturer] line: "%mfg% = Models, NTamd64, ..." */
static BOOL mfg_line(const WCHAR *key, const WCHAR *value, void *ctx)
{
    (void)key;
    Match *m = ctx;
    WCHAR base[96], deco[64];
    inf_field(value, 0, base, 96);
    BOOL decorated = FALSE;
    for (int i = 1; i < 8; i++) {
        inf_field(value, i, deco, 64);
        if (!deco[0]) break;
        if (prefix_w(deco, L"NTamd64")) {
            lstrcpyW(m->models, base);
            lstrcatW(m->models, L".");
            lstrcatW(m->models, deco);
            decorated = TRUE;
            break;
        }
    }
    if (!decorated) lstrcpyW(m->models, base);
    inf_lines(m->inf, m->models, model_line, m);
    return !m->found;
}

/* The folder dirid @id stands for (13: the package's store folder) */
static BOOL dirid_path(int id, const WCHAR *store, WCHAR *out)
{
    switch (id) {
    case 10: windir_w(out, L""); return TRUE;
    case 11: windir_w(out, L"\\System32"); return TRUE;
    case 12: windir_w(out, L"\\System32\\drivers"); return TRUE;
    case 13: lstrcpyW(out, store); return TRUE;
    }
    return FALSE;
}

/* @in with each %dirid% (%12%, %13%...) replaced */
static void expand_dirids(const WCHAR *in, const WCHAR *store, WCHAR *out, int cap)
{
    int n = 0;
    for (const WCHAR *s = in; *s && n < cap - 1; s++) {
        if (*s == '%' && s[1] >= '0' && s[1] <= '9') {
            const WCHAR *e = s + 1;
            int id = 0;
            while (*e >= '0' && *e <= '9') id = id * 10 + (*e++ - '0');
            WCHAR dir[MAX_PATH];
            if (*e == '%' && dirid_path(id, store, dir)) {
                for (WCHAR *d = dir; *d && n < cap - 1; d++) out[n++] = *d;
                s = e;
                continue;
            }
        }
        out[n++] = *s;
    }
    out[n] = 0;
}

typedef struct {
    const Inf *inf;
    const WCHAR *store;
    const WCHAR *section;                       /* the CopyFiles section */
    BOOL ok;
} Copy;

static BOOL copy_line(const WCHAR *key, const WCHAR *value, void *ctx)
{
    Copy *c = ctx;
    WCHAR dst_name[MAX_PATH], src_name[MAX_PATH], dirv[64], dir[MAX_PATH], from[MAX_PATH], to[MAX_PATH];
    /* "file" or "dest, src, ...": a line with no '=' comes as its value */
    const WCHAR *line = key[0] ? key : value;
    inf_field(line, 0, dst_name, MAX_PATH);
    inf_field(line, 1, src_name, MAX_PATH);
    if (!src_name[0]) lstrcpyW(src_name, dst_name);
    if (!dst_name[0]) return TRUE;
    if (!inf_get(c->inf, L"DestinationDirs", c->section, dirv, 64) &&
        !inf_get(c->inf, L"DestinationDirs", L"DefaultDestDir", dirv, 64))
        lstrcpyW(dirv, L"12");
    WCHAR idf[16];
    inf_field(dirv, 0, idf, 16);
    if (!dirid_path((int)num_w(idf), c->store, dir)) { c->ok = FALSE; return FALSE; }
    if ((int)num_w(idf) == 13) return TRUE;                       /* (runs from the driver store) */
    WCHAR sub[MAX_PATH];
    inf_field(dirv, 1, sub, MAX_PATH);
    if (sub[0]) { lstrcatW(dir, L"\\"); lstrcatW(dir, sub); CreateDirectoryW(dir, NULL); }
    /* the file's place in the store: the subfolder its SourceDisksFiles
     * line names (FTDI's amd64\ftdibus.sys), as staging kept it (driver.c) */
    WCHAR src_line[MAX_PATH], src_sub[MAX_PATH] = L"";
    if (inf_get(c->inf, L"SourceDisksFiles.amd64", src_name, src_line, MAX_PATH) ||
        inf_get(c->inf, L"SourceDisksFiles", src_name, src_line, MAX_PATH))
        inf_field(src_line, 1, src_sub, MAX_PATH);
    lstrcpyW(from, c->store);
    lstrcatW(from, L"\\");
    if (src_sub[0]) { lstrcatW(from, src_sub); lstrcatW(from, L"\\"); }
    lstrcatW(from, src_name);
    lstrcpyW(to, dir);
    lstrcatW(to, L"\\");
    lstrcatW(to, dst_name);
    if (!CopyFileW(from, to, FALSE)) { c->ok = FALSE; return FALSE; }
    return TRUE;
}

typedef struct {
    const Inf *inf;
    const WCHAR *store;
    WCHAR service[64];                          /* the device's own service */
    BOOL ok;
} Svc;

static BOOL service_line(const WCHAR *key, const WCHAR *value, void *ctx)
{
    Svc *s = ctx;
    if (!ieq_w(key, L"AddService")) return TRUE;
    WCHAR name[64], flags[32], sect[128], v[MAX_PATH], bin[MAX_PATH];
    inf_field(value, 0, name, 64);
    inf_field(value, 1, flags, 32);
    inf_field(value, 2, sect, 128);
    if (!name[0] || !sect[0]) return TRUE;
    DWORD type = 1, start = 3, err = 1;
    if (inf_get(s->inf, sect, L"ServiceType", v, 32)) type = num_w(v);
    if (inf_get(s->inf, sect, L"StartType", v, 32)) start = num_w(v);
    if (inf_get(s->inf, sect, L"ErrorControl", v, 32)) err = num_w(v);
    if (!inf_get(s->inf, sect, L"ServiceBinary", v, MAX_PATH)) { s->ok = FALSE; return FALSE; }
    expand_dirids(v, s->store, bin, MAX_PATH);
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm) { s->ok = FALSE; return FALSE; }
    SC_HANDLE h = CreateServiceW(scm, name, name, SERVICE_ALL_ACCESS, type, start, err, bin, NULL, NULL, NULL, NULL, NULL);
    if (!h && GetLastError() == ERROR_SERVICE_EXISTS) {
        h = OpenServiceW(scm, name, SERVICE_ALL_ACCESS);
        if (h && !ChangeServiceConfigW(h, type, start, err, bin, NULL, NULL, NULL, NULL, NULL, NULL)) {
            CloseServiceHandle(h);
            h = NULL;
        }
    }
    CloseServiceHandle(scm);
    if (!h) { s->ok = FALSE; return FALSE; }
    CloseServiceHandle(h);
    if (num_w(flags) & SPSVCINST_ASSOCSERVICE) lstrcpynW(s->service, name, 64);
    return TRUE;
}

typedef struct {
    HKEY key;                                   /* HKR: the device's Device Parameters, or its driver key */
    const Inf *inf;
} Reg;

/* "HKR, [subkey], [value], [flags], [data]" */
static BOOL addreg_line(const WCHAR *key, const WCHAR *value, void *ctx)
{
    Reg *r = ctx;
    WCHAR line[512], root[16], sub[256], name[128], fl[32], data[256];
    lstrcpynW(line, key, 512);
    if (value[0]) { lstrcatW(line, L","); lstrcatW(line, value); }
    inf_field(line, 0, root, 16);
    if (!ieq_w(root, L"HKR")) return TRUE;
    inf_field(line, 1, sub, 256);
    inf_field(line, 2, name, 128);
    inf_field(line, 3, fl, 32);
    inf_field(line, 4, data, 256);
    HKEY k = r->key;
    if (sub[0] && RegCreateKeyExW(r->key, sub, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) return TRUE;
    DWORD flags = num_w(fl);
    if ((flags & 0x00010001) == 0x00010001) {               /* FLG_ADDREG_TYPE_DWORD */
        DWORD d = num_w(data);
        RegSetValueExW(k, name[0] ? name : NULL, 0, REG_DWORD, (const BYTE *)&d, sizeof(d));
    } else if (flags & 0x00010000) {                        /* FLG_ADDREG_TYPE_MULTI_SZ */
        int n = lstrlenW(data);
        data[n + 1] = 0;
        RegSetValueExW(k, name[0] ? name : NULL, 0, REG_MULTI_SZ, (const BYTE *)data, (DWORD)((n + 2) * sizeof(WCHAR)));
    } else {
        RegSetValueExW(k, name[0] ? name : NULL, 0, REG_SZ, (const BYTE *)data, (DWORD)((lstrlenW(data) + 1) * sizeof(WCHAR)));
    }
    if (k != r->key) RegCloseKey(k);
    return TRUE;
}

typedef struct {
    const Inf *inf;
    const WCHAR *store;
    BOOL ok;
} Dir;

typedef struct {
    const Inf *inf;
    const WCHAR *store;
    HKEY params;                                /* the device's Device Parameters */
    BOOL ok;
} Wdf;

static void set_sz(HKEY k, const WCHAR *name, const WCHAR *v);

#define WUDF_SERVICES_KEY L"SYSTEM\\CurrentControlSet\\Services\\WUDF\\Services\\"

/* The .Wdf directives that set a device policy (Device Parameters\WUDF):
 * the directive, the value the framework reads, and the policy names in
 * the order of their numbers */
static const struct {
    const WCHAR *directive, *value, *names[3];
} wdf_policies[] = {
    { L"UmdfDirectHardwareAccess", L"DirectHardwareAccess", { L"RejectDirectHardwareAccess", L"AllowDirectHardwareAccess" } },
    { L"UmdfRegisterAccessMode", L"RegisterAccessMode", { L"RegisterAccessUsingSystemCall", L"RegisterAccessUsingUserModeMapping" } },
    { L"UmdfFileObjectPolicy", L"FileObjectPolicy", { L"RejectNullAndUnknownFileObjects", L"AllowNullAndUnknownFileObjects" } },
    { L"UmdfFsContextUsePolicy", L"FsContextUsePolicy", { L"DefaultFsContextUsePolicy", L"CanUseFsContext2", L"CannotUseFsContexts" } },
    { L"UmdfKernelModeClientPolicy", L"KernelModeClientPolicy", { L"RejectKernelModeClients", L"AllowKernelModeClients" } },
    { L"UmdfMethodNeitherAction", L"MethodNeitherAction", { L"UmdfMethodNeitherActionFail", L"UmdfMethodNeitherActionCopy" } },
};

static BOOL wdf_line(const WCHAR *key, const WCHAR *value, void *ctx)
{
    Wdf *w = ctx;
    if (ieq_w(key, L"UmdfService")) {
        /* "UmdfService = name, section": the service's DLL and the version it was built for */
        WCHAR name[64], sect[128], v[MAX_PATH], bin[MAX_PATH], k[200];
        inf_field(value, 0, name, 64);
        inf_field(value, 1, sect, 128);
        if (!name[0] || !sect[0] || !inf_get(w->inf, sect, L"ServiceBinary", v, MAX_PATH)) { w->ok = FALSE; return FALSE; }
        expand_dirids(v, w->store, bin, MAX_PATH);
        lstrcpyW(k, WUDF_SERVICES_KEY);
        lstrcatW(k, name);
        HKEY sk;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, k, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sk, NULL)) { w->ok = FALSE; return FALSE; }
        RegSetValueExW(sk, L"ImagePath", 0, REG_EXPAND_SZ, (const BYTE *)bin, (DWORD)((lstrlenW(bin) + 1) * sizeof(WCHAR)));
        if (inf_get(w->inf, sect, L"UmdfLibraryVersion", v, 64)) set_sz(sk, L"UmdfLibraryVersion", v);
        RegCloseKey(sk);
        return TRUE;
    }
    if (ieq_w(key, L"UmdfServiceOrder")) {
        /* "UmdfServiceOrder = name[, name...]": the stack's drivers, bottom first */
        WCHAR list[512], f[64];
        int n = 0;
        for (int i = 0; i < 8; i++) {
            inf_field(value, i, f, 64);
            if (!f[0]) break;
            lstrcpyW(list + n, f);
            n += lstrlenW(f) + 1;
        }
        list[n++] = 0;
        RegSetValueExW(w->params, L"UmdfServiceOrder", 0, REG_MULTI_SZ, (const BYTE *)list, (DWORD)(n * sizeof(WCHAR)));
        return TRUE;
    }
    for (int i = 0; i < (int)(sizeof(wdf_policies) / sizeof(wdf_policies[0])); i++) {
        if (!ieq_w(key, wdf_policies[i].directive)) continue;
        WCHAR f[64];
        inf_field(value, 0, f, 64);
        for (DWORD d = 0; d < 3 && wdf_policies[i].names[d]; d++) {
            if (!ieq_w(f, wdf_policies[i].names[d])) continue;
            HKEY wk;
            if (!RegCreateKeyExW(w->params, L"WUDF", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &wk, NULL)) {
                RegSetValueExW(wk, wdf_policies[i].value, 0, REG_DWORD, (const BYTE *)&d, sizeof(d));
                RegCloseKey(wk);
            }
        }
    }
    return TRUE;
}

/* "CopyFiles = Section[, Section...]" and "AddReg = ..." lines of DDInstall */
static BOOL install_line(const WCHAR *key, const WCHAR *value, void *ctx)
{
    Dir *d = ctx;
    if (!ieq_w(key, L"CopyFiles")) return TRUE;
    WCHAR sect[128];
    for (int i = 0; i < 8; i++) {
        inf_field(value, i, sect, 128);
        if (!sect[0]) break;
        Copy c = { d->inf, d->store, sect, TRUE };
        if (sect[0] == '@') {                               /* a single file */
            copy_line(sect + 1, L"", &c);
        } else {
            inf_lines(d->inf, sect, copy_line, &c);
        }
        if (!c.ok) { d->ok = FALSE; return FALSE; }
    }
    return TRUE;
}

static BOOL hw_line(const WCHAR *key, const WCHAR *value, void *ctx)
{
    Reg *r = ctx;
    if (!ieq_w(key, L"AddReg")) return TRUE;
    WCHAR sect[128];
    for (int i = 0; i < 8; i++) {
        inf_field(value, i, sect, 128);
        if (!sect[0]) break;
        inf_lines(r->inf, sect, addreg_line, r);
    }
    return TRUE;
}

/* A REG_SZ value of @root\@sub (or of @root) */
static BOOL get_sz(HKEY root, const WCHAR *sub, const WCHAR *name, WCHAR *out, DWORD cap)
{
    HKEY k = root;
    if (sub && RegOpenKeyExW(root, sub, 0, KEY_READ, &k)) return FALSE;
    DWORD type, n = (cap - 1) * sizeof(WCHAR);
    LONG e = RegQueryValueExW(k, name, NULL, &type, (BYTE *)out, &n);
    if (k != root) RegCloseKey(k);
    if (e || type != REG_SZ) return FALSE;
    out[n / sizeof(WCHAR)] = 0;
    return TRUE;
}

static void set_sz(HKEY k, const WCHAR *name, const WCHAR *v)
{
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)((lstrlenW(v) + 1) * sizeof(WCHAR)));
}

/* A port (class Ports) gets its COM name the way Windows' ports class
 * installer gives it one: the lowest number the COM name arbiter's ComDB
 * (a bitmap, bit n-1 for COMn) does not have taken, from COM3 (COM1 and
 * COM2 are the PC's own serial ports), kept in the device's Device
 * Parameters as PortName, where its serial driver reads the name of the
 * link it makes (\DosDevices\COMn); the device's FriendlyName names it
 * too.  A device that already has a PortName keeps it. */
#define PORTS_GUID L"{4d36e978-e325-11ce-bfc1-08002be10318}"
#define COMDB_KEY L"SYSTEM\\CurrentControlSet\\Control\\COM Name Arbiter"

static void com_name(HKEY dev, HKEY params, const WCHAR *desc)
{
    WCHAR port[16], friendly[300];
    if (!get_sz(params, NULL, L"PortName", port, 16) || !port[0]) {
        BYTE db[32];
        DWORD type = 0, size = sizeof(db);
        for (int i = 0; i < (int)sizeof(db); i++) db[i] = 0;
        HKEY arb;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, COMDB_KEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &arb, NULL)) return;
        if (RegQueryValueExW(arb, L"ComDB", NULL, &type, db, &size) || type != REG_BINARY) size = sizeof(db);
        int n = 3;
        while (n <= 256 && (db[(n - 1) / 8] >> ((n - 1) % 8) & 1)) n++;
        if (n > 256) { RegCloseKey(arb); return; }
        db[(n - 1) / 8] |= (BYTE)(1 << ((n - 1) % 8));
        RegSetValueExW(arb, L"ComDB", 0, REG_BINARY, db, sizeof(db));
        RegCloseKey(arb);
        port[0] = 'C'; port[1] = 'O'; port[2] = 'M';
        int k = 3;
        if (n >= 100) port[k++] = (WCHAR)('0' + n / 100);
        if (n >= 10) port[k++] = (WCHAR)('0' + n / 10 % 10);
        port[k++] = (WCHAR)('0' + n % 10);
        port[k] = 0;
        set_sz(params, L"PortName", port);
    }
    lstrcpynW(friendly, desc, 280);
    lstrcatW(friendly, L" (");
    lstrcatW(friendly, port);
    lstrcatW(friendly, L")");
    set_sz(dev, L"FriendlyName", friendly);
}

/*
 * Install published package @inf (C:\Windows\INF\oemN.inf) on device
 * @instance, through the model matching @hwid.  FALSE with the last error
 * set: ERROR_NOT_FOUND (no model for that ID), or why a file or the
 * service could not be set up.  The caller then starts the device (newdev).
 */
SETUPAPI BOOL WINAPI NovaInstallDeviceDriverW(LPCWSTR inf_path, LPCWSTR instance, LPCWSTR hwid)
{
    Inf *inf = inf_open(inf_path);
    if (!inf) return FALSE;
    Match m = { inf, hwid, L"", L"", L"", FALSE };
    inf_lines(inf, L"Manufacturer", mfg_line, &m);
    if (!m.found) { inf_close(inf); SetLastError(ERROR_NOT_FOUND); return FALSE; }

    /* the package's store folder */
    const WCHAR *published = inf_path;
    for (const WCHAR *c = inf_path; *c; c++) if (*c == '\\') published = c + 1;
    WCHAR k[MAX_PATH], pkg[MAX_PATH], store[MAX_PATH];
    lstrcpyW(k, DB_KEY);
    lstrcatW(k, published);
    if (!get_sz(HKEY_LOCAL_MACHINE, k, NULL, pkg, MAX_PATH)) {
        inf_close(inf);
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }
    windir_w(store, L"\\System32\\DriverStore\\FileRepository\\");
    lstrcatW(store, pkg);

    /* DDInstall: decorated for this machine when the INF has that */
    WCHAR ddi[160];
    lstrcpyW(ddi, m.section);
    lstrcatW(ddi, L".NTamd64");
    if (!has_section(inf, ddi)) {
        lstrcpyW(ddi, m.section);
        lstrcatW(ddi, L".NT");
        if (!has_section(inf, ddi)) lstrcpyW(ddi, m.section);
    }
    Dir d = { inf, store, TRUE };
    inf_lines(inf, ddi, install_line, &d);
    WCHAR sect[200];
    lstrcpyW(sect, ddi);
    lstrcatW(sect, L".Services");
    Svc s = { inf, store, L"", TRUE };
    if (d.ok) inf_lines(inf, sect, service_line, &s);
    if (!d.ok || !s.ok) { DWORD e = GetLastError(); inf_close(inf); SetLastError(e ? e : ERROR_FILE_NOT_FOUND); return FALSE; }

    /* the device's keys */
    WCHAR ek[400], cls[64] = L"", guid[64] = L"", prov[128] = L"", ver[128] = L"", mfgname[128] = L"";
    inf_get(inf, L"Version", L"Class", cls, 64);
    inf_get(inf, L"Version", L"ClassGuid", guid, 64);
    inf_get(inf, L"Version", L"Provider", prov, 128);
    inf_get(inf, L"Version", L"DriverVer", ver, 128);
    lstrcpyW(ek, ENUM_KEY);
    lstrcatW(ek, instance);
    HKEY dev;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, ek, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &dev, NULL)) {
        inf_close(inf);
        return FALSE;
    }
    /* its driver key: the one it had, or the class's next free number */
    WCHAR drvkey[128] = L"";
    if (!get_sz(dev, NULL, L"Driver", drvkey, 128) || !drvkey[0]) {
        for (int i = 0; i < 10000; i++) {
            WCHAR probe[300], num[8] = { (WCHAR)('0' + i / 1000), (WCHAR)('0' + i / 100 % 10),
                                         (WCHAR)('0' + i / 10 % 10), (WCHAR)('0' + i % 10), 0 };
            lstrcpyW(probe, CLASS_KEY);
            lstrcatW(probe, guid);
            lstrcatW(probe, L"\\");
            lstrcatW(probe, num);
            HKEY t;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, probe, 0, KEY_READ, &t)) {
                lstrcpyW(drvkey, guid);
                lstrcatW(drvkey, L"\\");
                lstrcatW(drvkey, num);
                break;
            }
            RegCloseKey(t);
        }
    }
    if (s.service[0]) set_sz(dev, L"Service", s.service);
    set_sz(dev, L"ClassGUID", guid);
    set_sz(dev, L"Class", cls);
    set_sz(dev, L"Driver", drvkey);
    set_sz(dev, L"DeviceDesc", m.desc);
    inf_get(inf, L"Strings", L"ManufacturerName", mfgname, 128);
    set_sz(dev, L"Mfg", mfgname[0] ? mfgname : prov);
    DWORD zero = 0;
    RegSetValueExW(dev, L"ConfigFlags", 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero));
    HKEY params;
    if (!RegCreateKeyExW(dev, L"Device Parameters", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &params, NULL)) {
        Reg r = { params, inf };
        lstrcpyW(sect, ddi);
        lstrcatW(sect, L".HW");
        inf_lines(inf, sect, hw_line, &r);
        if (ieq_w(guid, PORTS_GUID)) com_name(dev, params, m.desc);
        lstrcpyW(sect, ddi);
        lstrcatW(sect, L".Wdf");
        Wdf w = { inf, store, params, TRUE };
        inf_lines(inf, sect, wdf_line, &w);
        RegCloseKey(params);
        if (!w.ok) {
            RegCloseKey(dev);
            inf_close(inf);
            SetLastError(13);                   /* ERROR_INVALID_DATA */
            return FALSE;
        }
    }
    RegCloseKey(dev);

    WCHAR ck[300];
    lstrcpyW(ck, CLASS_KEY);
    lstrcatW(ck, drvkey);
    HKEY dk;
    if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, ck, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &dk, NULL)) {
        set_sz(dk, L"InfPath", published);
        set_sz(dk, L"InfSection", m.section);
        set_sz(dk, L"MatchingDeviceId", hwid);
        set_sz(dk, L"DriverDesc", m.desc);
        set_sz(dk, L"ProviderName", prov);
        WCHAR date[64], version[64];
        inf_field(ver, 0, date, 64);
        inf_field(ver, 1, version, 64);
        set_sz(dk, L"DriverDate", date);
        set_sz(dk, L"DriverVersion", version);
        Reg r = { dk, inf };                                /* DDInstall's AddReg: HKR is the driver key */
        inf_lines(inf, ddi, hw_line, &r);
        RegCloseKey(dk);
    }
    inf_close(inf);
    SetLastError(0);
    return TRUE;
}
