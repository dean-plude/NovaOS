/*
 * appx — installs Windows app packages (.msix, .appx), lists and removes
 * them and starts their applications, as Windows' Add-AppxPackage,
 * Get-AppxPackage, Remove-AppxPackage and the Start menu do:
 *
 *   appx [install] PACKAGE.msix [-AllowUnsigned]
 *   appx list
 *   appx remove NAME | FAMILY | FULLNAME
 *   appx start AUMID | NAME [ARGUMENTS...] [-Wait]
 *
 * A package is a zip (zip64 too) with its manifest (AppxManifest.xml), its
 * block map (AppxBlockMap.xml: a hash of every 64 KiB of every file) and
 * its content types; installing it
 * - checks every file against the block map (the files are taken only if
 *   each block's hash matches, and a file the block map does not name is
 *   refused), as Windows' deployment does before it trusts a byte;
 * - checks its signature (AppxSignature.p7x) as Windows' deployment does:
 *   wintrust's WinVerifyTrust verifies it (the APPX SIP's digests of the
 *   package's records, directory, content types, block map and code
 *   integrity catalog, the signer's signature and timestamp, its chain to
 *   a trusted root for code signing), and the manifest's publisher must
 *   be the signer's subject; a package whose signature fails is never
 *   installed.  An unsigned one only as Windows 11 takes one: with
 *   -AllowUnsigned and a publisher carrying Microsoft's "unsigned" OID
 *   (OID.2.25.311729368913984317654407730594956997722=1);
 * - finds each framework package it depends on (PackageDependency: the
 *   name, publisher and minimum version) among the installed ones, else
 *   refuses with the framework's name: install the framework first;
 * - unpacks the files into C:\Programs\WindowsApps\<package full name>;
 * - records the package in the current user's package repository
 *   (HKCU\...\AppModel\Repository: Packages, their Applications and the
 *   Families), which kernel32 (package.c: a process belongs to a package
 *   when its program is in the package's folder) and twinapi.appcore's
 *   ApplicationActivationManager read, and the package's in-process
 *   Windows Runtime classes (HKCU\Software\Classes\ActivatableClasses\
 *   Package\<full name>), which ole32's RoGetActivationFactory finds
 *   for the processes of the package and of those that depend on it;
 * - records it machine-wide too, with its dependencies, in
 *   HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModel\
 *   PackageRepository (Packages\<full name>: Path, Framework, and
 *   Dependencies, the frameworks' family names in the manifest's order;
 *   Families\<family>: the installed member's FullName and Path): the
 *   kernel's loader searches a packaged program's frameworks for its DLLs
 *   from there (its package graph, as Windows' loader does), and kernel32's
 *   GetCurrentPackageInfo lists the graph;
 * - names the package and its applications with the manifest's strings,
 *   an "ms-resource:" string looked up in the package's resources.pri
 *   (shlwapi's SHLoadIndirectString, through MRM.dll);
 * - adds each listed application to the Start menu.
 * A bundle (.msixbundle, .appxbundle: AppxMetadata\AppxBundleManifest.xml
 * naming the packages inside it) is checked as a package is (its block
 * map, its signature, its publisher), then the application package for
 * this processor (x64, else neutral, else x86) is installed from inside
 * it; a package inside a signed bundle is covered by the bundle's
 * signature when the bundle's block map names it, and checked by its own
 * when it has one.  The Store's bundles name only their manifest in their
 * block map: a package the bundle's manifest names may be outside it, and
 * then installs only by its own signature, verified as any package's
 * (unsigned, or signed by another, it is refused).  Either way a bundle's
 * package must have the bundle's publisher, as Windows requires.
 * A framework package (<Framework>true</Framework>) is installed the same
 * way; it has no applications, and it is not removed while a package that
 * depends on it is installed.
 * Installing and removing write C:\Programs and HKLM, so they need an
 * administrator (Windows' deployment service does that as the system for
 * any user; NovaOS has no such service yet).
 * A package's full name and family name are made as Windows makes them:
 * Name_Version_Architecture_ResourceId_PublisherId, the publisher id being
 * the first 64 bits of the SHA-256 of the publisher's name (UTF-16) in
 * Crockford's base 32.
 *
 * Not yet: a bundle's resource packages (languages, scales: the
 * application package's own resources are used), optional packages, and
 * the manifest's other extensions (file types, protocols, background
 * tasks, out-of-process servers).
 */
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG                     /* (the zlib decoder is what this uses) */
#include "../../third_party/stb/stb_image.h"

__declspec(dllimport) HRESULT WINAPI CreateXmlReader(REFIID iid, void **out, IMalloc *malloc);
__declspec(dllimport) IStream *WINAPI SHCreateMemStream(const BYTE *init, UINT n);
__declspec(dllimport) HRESULT WINAPI SHLoadIndirectString(LPCWSTR src, LPWSTR out, UINT n, void **reserved);
__declspec(dllimport) HRESULT WINAPI SHGetFolderPathW(HWND hwnd, int csidl, HANDLE token, DWORD flags, LPWSTR path);
__declspec(dllimport) LPWSTR *WINAPI CommandLineToArgvW(LPCWSTR cmd, int *argc);
__declspec(dllimport) LONG WINAPI BCryptOpenAlgorithmProvider(void **alg, LPCWSTR id, LPCWSTR impl, ULONG flags);
__declspec(dllimport) LONG WINAPI BCryptCloseAlgorithmProvider(void *alg, ULONG flags);
__declspec(dllimport) LONG WINAPI BCryptHash(void *alg, PUCHAR secret, ULONG slen, PUCHAR in, ULONG n, PUCHAR out, ULONG olen);

#define REPO_KEY L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository"
#define CLASSES_KEY L"Software\\Classes\\ActivatableClasses\\Package"
#define UNSIGNED_OID_A "OID.2.25.311729368913984317654407730594956997722=1"
#define UNSIGNED_OID L"" UNSIGNED_OID_A
#define CSIDL_PROGRAMS_ 0x02
#define CSIDL_PROGRAM_FILES_ 0x26
#define BLOCK 65536

static const GUID IID_IXmlReader_ = { 0x7279fc81, 0x709d, 0x4095, { 0xb6, 0x3d, 0x69, 0xfe, 0x4b, 0x0d, 0x90, 0x30 } };
static const GUID CLSID_ShellLink_ = { 0x00021401, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IShellLinkW_ = { 0x000214F9, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IPersistFile_ = { 0x0000010B, 0, 0, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID CLSID_ApplicationActivationManager_ = { 0x45BA127D, 0x10A8, 0x46EA, { 0x8A, 0xB7, 0x56, 0xEA, 0x90, 0x78, 0x94, 0x3C } };
static const GUID IID_IApplicationActivationManager_ = { 0x2E941141, 0x7F97, 0x4756, { 0xBA, 0x1D, 0x9D, 0xEC, 0xDE, 0x89, 0x4A, 0x3D } };

#define VT(o) (*(void ***)(o))

static void *xmalloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (!p) { printf("appx: out of memory\n"); ExitProcess(1); }
    return p;
}
static WCHAR *wdup(const WCHAR *s, size_t n)
{
    WCHAR *d = xmalloc((n + 1) * sizeof(WCHAR));
    memcpy(d, s, n * sizeof(WCHAR));
    return d;
}
static WCHAR *wcopy(const WCHAR *s) { return s ? wdup(s, wcslen(s)) : NULL; }

/* ---- the zip ---- */

typedef struct {
    WCHAR name[MAX_PATH];         /* as the block map names it: backslashes, %XX decoded */
    int method;
    unsigned long long csize, usize, at;   /* the compressed data's offset */
    BYTE *data;                   /* inflated, once read */
    int covered;                  /* named by the block map */
} Entry;

typedef struct {
    const BYTE *p;
    unsigned long long n;
    Entry *e;
    int count;
} Zip;

static unsigned u16(const BYTE *p) { return p[0] | p[1] << 8; }
static unsigned u32(const BYTE *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static unsigned long long u64(const BYTE *p) { return u32(p) | (unsigned long long)u32(p + 4) << 32; }

static int hexval(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* the zip's name (UTF-8, its reserved characters %XX-escaped as the
 * packaging format escapes them) as a path in the package */
static int entry_name(const BYTE *s, unsigned n, WCHAR out[MAX_PATH])
{
    char buf[MAX_PATH * 3];
    unsigned at = 0;
    for (unsigned i = 0; i < n; i++) {
        if (at >= sizeof buf - 1) return 0;
        if (s[i] == '%' && i + 2 < n && hexval(s[i + 1]) >= 0 && hexval(s[i + 2]) >= 0) {
            buf[at++] = (char)(hexval(s[i + 1]) << 4 | hexval(s[i + 2]));
            i += 2;
        } else
            buf[at++] = s[i] == '/' ? '\\' : (char)s[i];
    }
    int w = MultiByteToWideChar(CP_UTF8, 0, buf, (int)at, out, MAX_PATH - 1);
    if (w <= 0) return 0;
    out[w] = 0;
    return 1;
}

static int zip_open(Zip *z, const BYTE *p, unsigned long long n)
{
    z->p = p;
    z->n = n;
    if (n < 22) return 0;
    long long eocd = -1;
    for (long long i = (long long)n - 22; i >= 0 && i >= (long long)n - 22 - 65535; i--)
        if (u32(p + i) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) return 0;
    unsigned long long count = u16(p + eocd + 10), size = u32(p + eocd + 12), off = u32(p + eocd + 16);
    if (eocd >= 20 && u32(p + eocd - 20) == 0x07064b50) {       /* zip64 */
        unsigned long long z64 = u64(p + eocd - 12);
        if (z64 + 56 > n || u32(p + z64) != 0x06064b50) return 0;
        count = u64(p + z64 + 32);
        size = u64(p + z64 + 40);
        off = u64(p + z64 + 48);
    }
    if (off > n || size > n - off || count > size / 46) return 0;
    z->e = xmalloc((count ? count : 1) * sizeof(Entry));
    const BYTE *c = p + off, *end = p + off + size;
    for (z->count = 0; z->count < (int)count; z->count++) {
        Entry *e = &z->e[z->count];
        if (c + 46 > end || u32(c) != 0x02014b50) return 0;
        unsigned nl = u16(c + 28), xl = u16(c + 30), cl = u16(c + 32);
        if (c + 46 + nl + xl + cl > end) return 0;
        e->method = u16(c + 10);
        e->csize = u32(c + 20);
        e->usize = u32(c + 24);
        unsigned long long lho = u32(c + 42);
        for (const BYTE *x = c + 46 + nl; x + 4 <= c + 46 + nl + xl;) {   /* the zip64 sizes */
            unsigned id = u16(x), xn = u16(x + 2);
            const BYTE *v = x + 4, *ve = x + 4 + xn;
            if (ve > c + 46 + nl + xl) return 0;
            if (id == 1) {
                if (e->usize == 0xFFFFFFFF && v + 8 <= ve) { e->usize = u64(v); v += 8; }
                if (e->csize == 0xFFFFFFFF && v + 8 <= ve) { e->csize = u64(v); v += 8; }
                if (lho == 0xFFFFFFFF && v + 8 <= ve) lho = u64(v);
            }
            x = ve;
        }
        if (!entry_name(c + 46, nl, e->name)) return 0;
        if (lho + 30 > n || u32(p + lho) != 0x04034b50) return 0;
        e->at = lho + 30 + u16(p + lho + 26) + u16(p + lho + 28);
        if (e->at > n || e->csize > n - e->at) return 0;
        c += 46 + nl + xl + cl;
    }
    return 1;
}

static Entry *zip_find(Zip *z, const WCHAR *name)
{
    for (int i = 0; i < z->count; i++)
        if (!_wcsicmp(z->e[i].name, name)) return &z->e[i];
    return NULL;
}

/* the entry's bytes, inflated */
static BYTE *zip_read(Zip *z, Entry *e)
{
    if (e->data) return e->data;
    if (e->usize > 0x7FFFFFFF || e->csize > 0x7FFFFFFF) return NULL;
    BYTE *out = xmalloc((size_t)e->usize + 1);
    if (e->method == 0) {
        if (e->csize != e->usize) { free(out); return NULL; }
        memcpy(out, z->p + e->at, (size_t)e->usize);
    } else if (e->method == 8) {
        if (e->usize && stbi_zlib_decode_noheader_buffer((char *)out, (int)e->usize, (const char *)z->p + e->at,
                                                          (int)e->csize) != (int)e->usize) {
            free(out);
            return NULL;
        }
    } else {
        free(out);
        return NULL;
    }
    return e->data = out;
}

/* ---- XML (xmllite's IXmlReader) into a tree of elements ---- */

typedef struct Node {
    WCHAR *name;                  /* the local name */
    struct Node *parent, *child, *last, *next;
    int nattr;
    WCHAR **attr;                 /* local name, value, ... */
    WCHAR *text;
} Node;

typedef HRESULT (STDMETHODCALLTYPE *XrSetInput)(void *, IUnknown *);
typedef HRESULT (STDMETHODCALLTYPE *XrRead)(void *, int *);
typedef HRESULT (STDMETHODCALLTYPE *XrMove)(void *);
typedef HRESULT (STDMETHODCALLTYPE *XrString)(void *, const WCHAR **, UINT *);
typedef BOOL (STDMETHODCALLTYPE *XrBool)(void *);
enum { XR_SETINPUT = 3, XR_READ = 6, XR_FIRSTATTR = 8, XR_NEXTATTR = 9, XR_TOELEMENT = 11, XR_LOCALNAME = 14,
       XR_PREFIX = 15, XR_VALUE = 16, XR_ISEMPTY = 20 };

static Node *xml_parse(const BYTE *data, size_t n)
{
    IStream *s = SHCreateMemStream(data, (UINT)n);
    void *r = NULL;
    if (!s) return NULL;
    if (FAILED(CreateXmlReader(&IID_IXmlReader_, &r, NULL)) || !r ||
        FAILED(((XrSetInput)VT(r)[XR_SETINPUT])(r, (IUnknown *)s))) {
        if (r) ((IUnknown *)r)->lpVtbl->Release(r);
        s->lpVtbl->Release(s);
        return NULL;
    }
    Node *root = xmalloc(sizeof *root), *cur = root;
    root->name = wcopy(L"");
    int type;
    HRESULT hr;
    while ((hr = ((XrRead)VT(r)[XR_READ])(r, &type)) == S_OK) {
        const WCHAR *v, *pre;
        UINT vn, pn;
        if (type == 1) {                                            /* an element */
            Node *e = xmalloc(sizeof *e);
            ((XrString)VT(r)[XR_LOCALNAME])(r, &v, &vn);
            e->name = wdup(v, vn);
            for (HRESULT a = ((XrMove)VT(r)[XR_FIRSTATTR])(r); a == S_OK; a = ((XrMove)VT(r)[XR_NEXTATTR])(r)) {
                ((XrString)VT(r)[XR_PREFIX])(r, &pre, &pn);
                ((XrString)VT(r)[XR_LOCALNAME])(r, &v, &vn);
                if ((pn == 5 && !wcsncmp(pre, L"xmlns", 5)) || (!pn && vn == 5 && !wcsncmp(v, L"xmlns", 5)))
                    continue;                                       /* (namespace declarations) */
                e->attr = realloc(e->attr, (e->nattr + 2) * sizeof(WCHAR *));
                e->attr[e->nattr++] = wdup(v, vn);
                ((XrString)VT(r)[XR_VALUE])(r, &v, &vn);
                e->attr[e->nattr++] = wdup(v, vn);
            }
            ((XrMove)VT(r)[XR_TOELEMENT])(r);
            e->parent = cur;
            if (cur->last) cur->last->next = e; else cur->child = e;
            cur->last = e;
            if (!((XrBool)VT(r)[XR_ISEMPTY])(r)) cur = e;
        } else if (type == 15) {                                    /* its end */
            if (cur->parent) cur = cur->parent;
        } else if (type == 3 || type == 4) {                        /* text */
            ((XrString)VT(r)[XR_VALUE])(r, &v, &vn);
            size_t had = cur->text ? wcslen(cur->text) : 0;
            WCHAR *t = xmalloc((had + vn + 1) * sizeof(WCHAR));
            if (had) memcpy(t, cur->text, had * sizeof(WCHAR));
            memcpy(t + had, v, vn * sizeof(WCHAR));
            free(cur->text);
            cur->text = t;
        }
    }
    ((IUnknown *)r)->lpVtbl->Release(r);
    s->lpVtbl->Release(s);
    return FAILED(hr) ? NULL : root;
}

static Node *child(Node *n, const WCHAR *name)
{
    for (Node *c = n ? n->child : NULL; c; c = c->next)
        if (!wcscmp(c->name, name)) return c;
    return NULL;
}
static Node *sibling(Node *n, const WCHAR *name)
{
    for (Node *c = n ? n->next : NULL; c; c = c->next)
        if (!wcscmp(c->name, name)) return c;
    return NULL;
}
static const WCHAR *attr(Node *n, const WCHAR *name)
{
    for (int i = 0; n && i + 1 < n->nattr; i += 2)
        if (!wcscmp(n->attr[i], name)) return n->attr[i + 1];
    return NULL;
}
static const WCHAR *text(Node *n) { return n && n->text ? n->text : L""; }

/* ---- hashes ---- */

static int hash(const WCHAR *alg, const BYTE *p, size_t n, BYTE *out, ULONG len)
{
    void *h = NULL;
    if (BCryptOpenAlgorithmProvider(&h, alg, NULL, 0)) return 0;
    LONG st = BCryptHash(h, NULL, 0, (PUCHAR)p, (ULONG)n, out, len);
    BCryptCloseAlgorithmProvider(h, 0);
    return st == 0;
}

static void base64(const BYTE *p, int n, WCHAR *out)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int at = 0;
    for (int i = 0; i < n; i += 3) {
        unsigned v = p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        out[at++] = t[v >> 18 & 63];
        out[at++] = t[v >> 12 & 63];
        out[at++] = i + 1 < n ? t[v >> 6 & 63] : '=';
        out[at++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[at] = 0;
}

/* ---- the package ---- */

typedef struct {
    WCHAR *id, *exe, *entry, *behavior, *display, *show;
} App;

typedef struct {
    WCHAR *dll, *cls[64], *threading[64];
    int ncls;
} Server;

typedef struct {
    WCHAR *name, *publisher, *minver;
    WCHAR family[300], full[300];          /* (the installed one it was found as) */
} Dependency;

typedef struct {
    Zip zip;
    Node *manifest;
    WCHAR *name, *publisher, *version, *arch, *resource, *display, *pubdisplay;
    WCHAR family[300], full[300], pubid[16];
    int framework;
    App apps[16];
    int napps;
    Server servers[16];
    int nservers;
    Dependency deps[16];
    int ndeps;
} Package;

/* the publisher id: SHA-256 of the publisher (UTF-16LE), its first 8
 * bytes in Crockford's base 32 (13 characters, the last from one bit) */
static int publisher_id(const WCHAR *publisher, WCHAR out[16])
{
    static const char digits[] = "0123456789abcdefghjkmnpqrstvwxyz";
    BYTE h[32];
    if (!hash(L"SHA256", (const BYTE *)publisher, wcslen(publisher) * sizeof(WCHAR), h, 32)) return 0;
    unsigned long long v = 0;
    for (int i = 0; i < 8; i++) v = v << 8 | h[i];
    for (int i = 0; i < 13; i++) {
        int shift = 59 - 5 * i;                    /* 64 bits and a zero bit, 5 at a time */
        out[i] = digits[(shift >= 0 ? v >> shift : v << -shift) & 31];
    }
    out[13] = 0;
    return 1;
}

static unsigned long long version_number(const WCHAR *v)
{
    unsigned long long out = 0;
    for (int i = 0; i < 4; i++) {
        out = out << 16 | (wcstoul(v, (WCHAR **)&v, 10) & 0xFFFF);
        if (*v == '.') v++;
    }
    return out;
}

static int fail(const char *fmt, const WCHAR *what)
{
    printf("appx: ");
    printf(fmt, what);
    printf("\n");
    return 0;
}

/* @fn (a bundle manifest's FileName): one of @bundle's packages */
static int bundle_names(Node *bundle, const WCHAR *fn)
{
    for (Node *p = child(child(bundle, L"Packages"), L"Package"); p; p = sibling(p, L"Package")) {
        const WCHAR *f = attr(p, L"FileName");
        WCHAR name[MAX_PATH];
        if (!f || wcslen(f) >= MAX_PATH) continue;
        wcscpy(name, f);
        for (WCHAR *c = name; *c; c++) if (*c == '/') *c = '\\';
        if (!wcscmp(name, fn)) return 1;
    }
    return 0;
}

/* every file against the block map; in a bundle (@bundle, its manifest's
 * Bundle element) a package its manifest names may be outside the block
 * map, as the Store's bundles are (their block map covers only the bundle
 * manifest): such a package is left uncovered, and installs only by its
 * own signature (install_bundle) */
static int check_blockmap(Package *pk, Node *bundle)
{
    Entry *bme = zip_find(&pk->zip, L"AppxBlockMap.xml");
    BYTE *bm = bme ? zip_read(&pk->zip, bme) : NULL;
    Node *root = bm ? xml_parse(bm, (size_t)bme->usize) : NULL;
    Node *map = child(root, L"BlockMap");
    if (!map) return fail("no block map (AppxBlockMap.xml): not an app package", L"");
    const WCHAR *method = attr(map, L"HashMethod"), *alg;
    int hlen;
    if (method && !wcscmp(method, L"http://www.w3.org/2001/04/xmlenc#sha256")) alg = L"SHA256", hlen = 32;
    else if (method && !wcscmp(method, L"http://www.w3.org/2001/04/xmldsig-more#sha384")) alg = L"SHA384", hlen = 48;
    else if (method && !wcscmp(method, L"http://www.w3.org/2001/04/xmlenc#sha512")) alg = L"SHA512", hlen = 64;
    else return fail("the block map's hash method %ls is not one of SHA-256, -384, -512", method ? method : L"(none)");
    bme->covered = 1;
    for (Node *f = child(map, L"File"); f; f = sibling(f, L"File")) {
        const WCHAR *name = attr(f, L"Name"), *size = attr(f, L"Size");
        Entry *e = name ? zip_find(&pk->zip, name) : NULL;
        if (!e) return fail("%ls: in the block map but not in the package", name ? name : L"(no name)");
        if (!size || wcstoull(size, NULL, 10) != e->usize) return fail("%ls: its size is not the block map's", name);
        BYTE *data = zip_read(&pk->zip, e);
        if (!data) return fail("%ls: cannot be read (damaged, or not deflated or stored)", name);
        unsigned long long at = 0;
        for (Node *b = child(f, L"Block"); b; b = sibling(b, L"Block"), at += BLOCK) {
            BYTE h[64];
            WCHAR b64[100];
            const WCHAR *want = attr(b, L"Hash");
            unsigned long long n = e->usize - at < BLOCK ? e->usize - at : BLOCK;
            if (at >= e->usize && e->usize) return fail("%ls: more blocks than data", name);
            if (!want || !hash(alg, data + at, (size_t)n, h, hlen)) return fail("%ls: a block cannot be hashed", name);
            base64(h, hlen, b64);
            if (wcscmp(b64, want)) return fail("%ls: its contents do not match the block map: the package is damaged "
                                               "or was changed", name);
        }
        if (at < e->usize) return fail("%ls: fewer blocks than data", name);
        e->covered = 1;
    }
    /* (the content types, the signature and, in a signed package, its code
     * integrity catalog are outside the block map: the signature's own
     * digests cover them) */
    int sig = zip_find(&pk->zip, L"AppxSignature.p7x") != NULL;
    for (int i = 0; i < pk->zip.count; i++) {
        const WCHAR *n = pk->zip.e[i].name;
        if (!pk->zip.e[i].covered && wcscmp(n, L"[Content_Types].xml") && wcscmp(n, L"AppxSignature.p7x") &&
            !(sig && !wcscmp(n, L"AppxMetadata\\CodeIntegrity.cat")) && !(bundle && bundle_names(bundle, n)))
            return fail("%ls: in the package but not in its block map", n);
    }
    return 1;
}

static int read_manifest(Package *pk)
{
    Entry *e = zip_find(&pk->zip, L"AppxManifest.xml");
    if (!e) {
        if (zip_find(&pk->zip, L"AppxMetadata\\AppxBundleManifest.xml"))
            return fail("a bundle (.msixbundle, .appxbundle): bundles are not installed yet", L"");
        return fail("no manifest (AppxManifest.xml): not an app package", L"");
    }
    BYTE *data = zip_read(&pk->zip, e);
    pk->manifest = data ? xml_parse(data, (size_t)e->usize) : NULL;
    Node *p = child(pk->manifest, L"Package"), *id = child(p, L"Identity");
    if (!id) return fail("the manifest has no package identity", L"");
    pk->name = wcopy(attr(id, L"Name"));
    pk->publisher = wcopy(attr(id, L"Publisher"));
    pk->version = wcopy(attr(id, L"Version"));
    pk->arch = wcopy(attr(id, L"ProcessorArchitecture") ? attr(id, L"ProcessorArchitecture") : L"neutral");
    pk->resource = wcopy(attr(id, L"ResourceId") ? attr(id, L"ResourceId") : L"");
    if (!pk->name || !pk->publisher || !pk->version) return fail("the identity needs Name, Publisher and Version", L"");
    for (WCHAR *a = pk->arch; *a; a++) *a = (WCHAR)towlower(*a);
    if (wcscmp(pk->arch, L"x64") && wcscmp(pk->arch, L"x86") && wcscmp(pk->arch, L"neutral"))
        return fail("%ls: a package for this processor cannot run here", pk->arch);
    Node *props = child(p, L"Properties");
    pk->display = wcopy(text(child(props, L"DisplayName")));
    pk->pubdisplay = wcopy(text(child(props, L"PublisherDisplayName")));
    pk->framework = !_wcsicmp(text(child(props, L"Framework")), L"true");
    Node *deps = child(p, L"Dependencies");
    for (Node *d = child(deps, L"PackageDependency"); d && pk->ndeps < 16; d = sibling(d, L"PackageDependency")) {
        Dependency *dp = &pk->deps[pk->ndeps++];
        dp->name = wcopy(attr(d, L"Name"));
        dp->publisher = wcopy(attr(d, L"Publisher"));
        dp->minver = wcopy(attr(d, L"MinVersion") ? attr(d, L"MinVersion") : L"0.0.0.0");
        WCHAR id[16];
        if (!dp->name || !dp->publisher) return fail("a package dependency needs Name and Publisher", L"");
        if (!publisher_id(dp->publisher, id)) return fail("%ls: cannot hash the publisher", dp->publisher);
        swprintf(dp->family, 300, L"%ls_%ls", dp->name, id);
    }
    for (Node *a = child(child(p, L"Applications"), L"Application"); a && pk->napps < 16;
         a = sibling(a, L"Application")) {
        App *ap = &pk->apps[pk->napps++];
        Node *ve = child(a, L"VisualElements");
        ap->id = wcopy(attr(a, L"Id"));
        ap->exe = wcopy(attr(a, L"Executable"));
        ap->entry = wcopy(attr(a, L"EntryPoint") ? attr(a, L"EntryPoint") : L"");
        ap->display = wcopy(attr(ve, L"DisplayName") ? attr(ve, L"DisplayName") : pk->display);
        ap->show = wcopy(attr(ve, L"AppListEntry") ? attr(ve, L"AppListEntry") : L"default");
        /* uap10:RuntimeBehavior, or what the entry point implies (a full
         * trust program, or a UWP app's CoreApplication) */
        const WCHAR *rb = attr(a, L"RuntimeBehavior");
        ap->behavior = wcopy(rb ? rb : !wcscmp(ap->entry, L"Windows.FullTrustApplication") ? L"packagedClassicApp"
                                                                                       : L"windowsApp");
        if (!ap->id || !ap->exe) return fail("%ls: an application needs Id and Executable", ap->id ? ap->id : L"");
    }
    /* the in-process Windows Runtime servers, the package's or an application's */
    for (int pass = 0; pass < 1 + pk->napps; pass++) {
        Node *exts = NULL;
        if (!pass) exts = child(p, L"Extensions");
        else {
            Node *a = child(child(p, L"Applications"), L"Application");
            for (int i = 1; i < pass && a; i++) a = sibling(a, L"Application");
            exts = child(a, L"Extensions");
        }
        for (Node *x = child(exts, L"Extension"); x; x = sibling(x, L"Extension")) {
            const WCHAR *cat = attr(x, L"Category");
            Node *srv = child(x, L"InProcessServer");
            if (!cat || wcscmp(cat, L"windows.activatableClass.inProcessServer") || !srv || pk->nservers >= 16)
                continue;
            Server *s = &pk->servers[pk->nservers++];
            s->dll = wcopy(text(child(srv, L"Path")));
            for (Node *c = child(srv, L"ActivatableClass"); c && s->ncls < 64; c = sibling(c, L"ActivatableClass")) {
                s->cls[s->ncls] = wcopy(attr(c, L"ActivatableClassId"));
                s->threading[s->ncls] = wcopy(attr(c, L"ThreadingModel") ? attr(c, L"ThreadingModel") : L"both");
                if (s->cls[s->ncls]) s->ncls++;
            }
        }
    }
    if (!publisher_id(pk->publisher, pk->pubid)) return fail("%ls: cannot hash the publisher", pk->publisher);
    swprintf(pk->family, 300, L"%ls_%ls", pk->name, pk->pubid);
    swprintf(pk->full, 300, L"%ls_%ls_%ls_%ls_%ls", pk->name, pk->version, pk->arch, pk->resource, pk->pubid);
    return 1;
}

/* ---- files ---- */

static void make_dirs(WCHAR *path)            /* every folder of @path but its last part */
{
    for (WCHAR *p = path + 3; *p; p++)
        if (*p == '\\') {
            *p = 0;
            CreateDirectoryW(path, NULL);
            *p = '\\';
        }
}

static int write_file(const WCHAR *path, const BYTE *p, size_t n)
{
    WCHAR tmp[MAX_PATH * 2];
    wcscpy(tmp, path);
    make_dirs(tmp);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD done = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    BOOL ok = !n || WriteFile(h, p, (DWORD)n, &done, NULL);
    CloseHandle(h);
    return ok && done == n;
}

static void delete_tree(const WCHAR *dir)
{
    WCHAR pat[MAX_PATH * 2], sub[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    swprintf(pat, MAX_PATH * 2, L"%ls\\*", dir);
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            swprintf(sub, MAX_PATH * 2, L"%ls\\%ls", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) delete_tree(sub);
            else DeleteFileW(sub);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    RemoveDirectoryW(dir);
}

/* a name the package may use: within its folder */
static int safe_name(const WCHAR *n)
{
    if (!n[0] || n[0] == '\\' || wcschr(n, ':')) return 0;
    for (const WCHAR *p = n; *p;) {
        const WCHAR *e = wcschr(p, '\\');
        size_t len = e ? (size_t)(e - p) : wcslen(p);
        if (!len || (len == 2 && p[0] == '.' && p[1] == '.') || (len == 1 && p[0] == '.')) return 0;
        p += len + (e ? 1 : 0);
    }
    return 1;
}

/* ---- the repository ---- */

static HKEY repo_key(const WCHAR *fmt, const WCHAR *a, const WCHAR *b, int create)
{
    WCHAR k[600], sub[400];
    HKEY h = NULL;
    swprintf(sub, 400, fmt, a, b);
    swprintf(k, 600, L"%ls\\%ls", REPO_KEY, sub);
    if (create ? RegCreateKeyExW(HKEY_CURRENT_USER, k, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &h, NULL)
               : RegOpenKeyExW(HKEY_CURRENT_USER, k, 0, KEY_READ, &h))
        return NULL;
    return h;
}

static void set_str(HKEY k, const WCHAR *name, const WCHAR *v)
{
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)(wcslen(v) + 1) * sizeof(WCHAR));
}
static void set_dword(HKEY k, const WCHAR *name, DWORD v) { RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE *)&v, 4); }

static int get_str(HKEY k, const WCHAR *name, WCHAR *out, DWORD cap)
{
    DWORD size = cap * sizeof(WCHAR);
    out[0] = 0;
    return !RegGetValueW(k, NULL, name, RRF_RT_REG_SZ, NULL, out, &size);
}

/* the manifest string @s, "ms-resource:" strings looked up in the package's resources.pri */
static void resolve(const WCHAR *full, const WCHAR *s, WCHAR *out, UINT cap)
{
    WCHAR src[600];
    wcsncpy(out, s, cap - 1);
    out[cap - 1] = 0;
    if (_wcsnicmp(s, L"ms-resource:", 12)) return;
    swprintf(src, 600, L"@{%ls?%ls}", full, s);
    if (FAILED(SHLoadIndirectString(src, out, cap, NULL))) {
        wcsncpy(out, s, cap - 1);
        out[cap - 1] = 0;
    }
}

static int start_menu_link(const WCHAR *exe, const WCHAR *dir, const WCHAR *name, const WCHAR *desc, WCHAR *lnk)
{
    typedef HRESULT (STDMETHODCALLTYPE *SetStr)(void *, LPCWSTR);
    typedef HRESULT (STDMETHODCALLTYPE *SetIcon)(void *, LPCWSTR, int);
    typedef HRESULT (STDMETHODCALLTYPE *Qi)(void *, REFIID, void **);
    typedef HRESULT (STDMETHODCALLTYPE *Save)(void *, LPCOLESTR, BOOL);
    WCHAR folder[MAX_PATH], clean[MAX_PATH];
    void *sl = NULL, *pf = NULL;
    int at = 0;
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS_, NULL, 0, folder))) return 0;
    for (const WCHAR *p = name; *p && at < 100; p++)            /* (a file name) */
        clean[at++] = wcschr(L"\\/:*?\"<>|", *p) ? '_' : *p;
    clean[at] = 0;
    swprintf(lnk, MAX_PATH, L"%ls\\%ls.lnk", folder, clean);
    if (FAILED(CoCreateInstance(&CLSID_ShellLink_, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW_, &sl)) || !sl) return 0;
    ((SetStr)VT(sl)[20])(sl, exe);                             /* SetPath */
    ((SetStr)VT(sl)[9])(sl, dir);                              /* SetWorkingDirectory */
    ((SetStr)VT(sl)[7])(sl, desc);                             /* SetDescription */
    ((SetIcon)VT(sl)[17])(sl, exe, 0);                         /* SetIconLocation */
    HRESULT hr = ((Qi)VT(sl)[0])(sl, &IID_IPersistFile_, &pf);
    if (SUCCEEDED(hr)) {
        hr = ((Save)VT(pf)[6])(pf, lnk, TRUE);
        ((IUnknown *)pf)->lpVtbl->Release(pf);
    }
    ((IUnknown *)sl)->lpVtbl->Release(sl);
    return SUCCEEDED(hr);
}

/* @full unregistered and its folder deleted: 0 if the folder is still
 * there.  A program of the package that has just exited may still hold
 * its files for a moment (its image is unmapped after the process is
 * signalled), so the delete is tried again for a few seconds. */
static void machine_forget(const WCHAR *full);
static int remove_package(const WCHAR *full)
{
    WCHAR k[600], root[MAX_PATH] = L"", lnk[MAX_PATH], fam[300];
    HKEY h = repo_key(L"Packages\\%ls%ls", full, L"", 0);
    if (h) {
        get_str(h, L"PackageRootFolder", root, MAX_PATH);
        HKEY apps;
        if (!RegOpenKeyExW(h, L"Applications", 0, KEY_READ, &apps)) {
            WCHAR id[128];
            for (DWORD i = 0; ; i++) {
                DWORD n = 128;
                HKEY a;
                if (RegEnumKeyExW(apps, i, id, &n, NULL, NULL, NULL, NULL)) break;
                if (!RegOpenKeyExW(apps, id, 0, KEY_READ, &a)) {
                    if (get_str(a, L"StartMenuLink", lnk, MAX_PATH) && lnk[0]) DeleteFileW(lnk);
                    RegCloseKey(a);
                }
            }
            RegCloseKey(apps);
        }
        RegCloseKey(h);
    }
    swprintf(k, 600, L"%ls\\Packages\\%ls", REPO_KEY, full);
    RegDeleteTreeW(HKEY_CURRENT_USER, k);
    RegDeleteKeyW(HKEY_CURRENT_USER, k);
    /* its family: Name_PublisherId of Name_Version_Arch_ResourceId_PublisherId */
    const WCHAR *us = wcschr(full, '_'), *last = wcsrchr(full, '_');
    if (us && last) {
        swprintf(fam, 300, L"%.*ls%ls", (int)(us - full), full, last);
        swprintf(k, 600, L"%ls\\Families\\%ls\\%ls", REPO_KEY, fam, full);
        RegDeleteKeyW(HKEY_CURRENT_USER, k);
        swprintf(k, 600, L"%ls\\Families\\%ls", REPO_KEY, fam);
        RegDeleteKeyW(HKEY_CURRENT_USER, k);                    /* (only once empty) */
    }
    machine_forget(full);
    swprintf(k, 600, L"%ls\\%ls", CLASSES_KEY, full);
    RegDeleteTreeW(HKEY_CURRENT_USER, k);
    RegDeleteKeyW(HKEY_CURRENT_USER, k);
    for (int tries = 0; root[0] && tries < 100; tries++) {
        delete_tree(root);
        if (GetFileAttributesW(root) == INVALID_FILE_ATTRIBUTES) return 1;
        Sleep(100);
    }
    return !root[0];
}

/* the installed packages of @family, by full name */
static int family_packages(const WCHAR *family, WCHAR out[][300], int cap)
{
    HKEY h = repo_key(L"Families\\%ls%ls", family, L"", 0);
    int n = 0;
    if (!h) return 0;
    for (DWORD i = 0; n < cap; i++) {
        DWORD len = 300;
        if (RegEnumKeyExW(h, i, out[n], &len, NULL, NULL, NULL, NULL)) break;
        n++;
    }
    RegCloseKey(h);
    return n;
}

/* ---- signatures (wintrust's WinVerifyTrust, the APPX SIP) ---- */

typedef struct { DWORD cbData; BYTE *pbData; } Blob_;
typedef struct {
    DWORD dwVersion;
    Blob_ SerialNumber;
    struct { LPSTR pszObjId; Blob_ Parameters; } SignatureAlgorithm;
    Blob_ Issuer;
    FILETIME NotBefore, NotAfter;
    Blob_ Subject;
} CertInfo_;
typedef struct { DWORD dwCertEncodingType; BYTE *pbCertEncoded; DWORD cbCertEncoded; CertInfo_ *pCertInfo; HANDLE hCertStore; } CertContext_;
typedef struct { DWORD cbStruct; const CertContext_ *pCert; } ProvCert_;
typedef struct { DWORD cbStruct; GUID gSubject; LPCWSTR pcwszDisplayName; DWORD cbMemObject; BYTE *pbMemObject;
                 DWORD cbMemSignedMsg; BYTE *pbMemSignedMsg; } BlobInfo_;
typedef struct {
    DWORD cbStruct;
    LPVOID pPolicyCallbackData, pSIPClientData;
    DWORD dwUIChoice, fdwRevocationChecks, dwUnionChoice;
    void *pInfo;
    DWORD dwStateAction;
    HANDLE hWVTStateData;
    WCHAR *pwszURLReference;
    DWORD dwProvFlags, dwUIContext;
    void *pSignatureSettings;
} TrustData_;
__declspec(dllimport) LONG WINAPI WinVerifyTrust(HWND w, GUID *action, LPVOID data);
__declspec(dllimport) PVOID WINAPI WTHelperProvDataFromStateData(HANDLE h);
__declspec(dllimport) PVOID WINAPI WTHelperGetProvSignerFromChain(PVOID prov, DWORD signer, BOOL counter, DWORD index);
__declspec(dllimport) PVOID WINAPI WTHelperGetProvCertFromChain(PVOID signer, DWORD index);
__declspec(dllimport) DWORD WINAPI CertNameToStrW(DWORD enc, Blob_ *name, DWORD type, LPWSTR s, DWORD n);
static GUID GENERIC_VERIFY_V2_ = { 0x00AAC56B, 0xCD44, 0x11D0, { 0x8C, 0xC2, 0x00, 0xC0, 0x4F, 0xC2, 0x95, 0xEE } };

/* @s with the spaces around its separators taken out and its attribute
 * names in capitals, for comparing names ("CN=A, O=B" is "CN=A,O=B") */
static void name_key(const WCHAR *s, WCHAR *out, size_t cap)
{
    size_t o = 0;
    int in_value = 0;
    for (; *s && o + 1 < cap; s++) {
        if (*s == ',' || *s == '+') {
            while (o && out[o - 1] == ' ') o--;
            out[o++] = *s;
            while (s[1] == ' ') s++;
            in_value = 0;
        } else if (*s == '=' && !in_value) {
            while (o && out[o - 1] == ' ') o--;
            out[o++] = '=';
            while (s[1] == ' ') s++;
            in_value = 1;
        } else out[o++] = in_value ? *s : (WCHAR)towupper(*s);
    }
    out[o] = 0;
}

/* @data's signature verified, its signer the publisher @publisher; 1 when
 * it verifies, 0 (said why) when not */
static int check_signature(const BYTE *data, size_t n, const WCHAR *publisher, const WCHAR *what)
{
    BlobInfo_ bi = { sizeof bi };
    bi.cbMemObject = (DWORD)n;
    bi.pbMemObject = (BYTE *)data;
    bi.pcwszDisplayName = what;
    TrustData_ td = { sizeof td };
    td.dwUIChoice = 2;                                  /* WTD_UI_NONE */
    td.dwUnionChoice = 3;                               /* WTD_CHOICE_BLOB */
    td.pInfo = &bi;
    td.dwStateAction = 1;                               /* WTD_STATEACTION_VERIFY */
    LONG r = WinVerifyTrust(NULL, &GENERIC_VERIFY_V2_, &td);
    WCHAR subject[600] = L"", a[600], b[600];
    if (!r) {
        void *sg = WTHelperGetProvSignerFromChain(WTHelperProvDataFromStateData(td.hWVTStateData), 0, FALSE, 0);
        ProvCert_ *pc = sg ? WTHelperGetProvCertFromChain(sg, 0) : NULL;
        if (pc && pc->pCert)                            /* CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG */
            CertNameToStrW(1, &pc->pCert->pCertInfo->Subject, 3 | 0x02000000, subject, 600);
    }
    td.dwStateAction = 2;                               /* WTD_STATEACTION_CLOSE */
    WinVerifyTrust(NULL, &GENERIC_VERIFY_V2_, &td);
    if (r) {
        const char *why = (unsigned long)r == 0x800B0109 ? "its signer's certificate does not chain to a trusted root "
                                                           "(trust the signer's root certificate to install it)" :
                          (unsigned long)r == 0x80096010 ? "its contents do not match its signature: it was changed "
                                                           "after it was signed" :
                          (unsigned long)r == 0x800B0101 ? "its signer's certificate has expired and the signature "
                                                           "has no timestamp" :
                          (unsigned long)r == 0x800B0100 ? "its signature cannot be read" :
                          "its signature does not verify";
        printf("appx: %ls: %s (0x%08lx), so it is not installed\n", what, why, (unsigned long)r);
        return 0;
    }
    name_key(subject, a, 600);
    name_key(publisher, b, 600);
    if (wcscmp(a, b)) {
        printf("appx: %ls: its publisher (%ls) is not its signer (%ls), so it is not installed\n", what, publisher, subject);
        return 0;
    }
    return 1;
}

/* ---- the machine's package repository (the loader's package graphs) ---- */

#define MACHINE_REPO L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModel\\PackageRepository"

static HKEY machine_key(const WCHAR *kind, const WCHAR *name, const WCHAR *sub, int create)
{
    WCHAR k[700];
    HKEY h = NULL;
    swprintf(k, 700, L"%ls\\%ls\\%ls%ls%ls", MACHINE_REPO, kind, name, sub ? L"\\" : L"", sub ? sub : L"");
    if (create ? RegCreateKeyExW(HKEY_LOCAL_MACHINE, k, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &h, NULL)
               : RegOpenKeyExW(HKEY_LOCAL_MACHINE, k, 0, KEY_READ, &h))
        return NULL;
    return h;
}

static void machine_forget(const WCHAR *full)
{
    WCHAR k[700], fam[300], have[300];
    const WCHAR *us = wcschr(full, '_'), *last = wcsrchr(full, '_');
    swprintf(k, 700, L"%ls\\Packages\\%ls", MACHINE_REPO, full);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, k);
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, k);
    if (!us || !last) return;
    swprintf(fam, 300, L"%.*ls%ls", (int)(us - full), full, last);
    HKEY f = machine_key(L"Families", fam, NULL, 0);
    int mine = f && get_str(f, L"FullName", have, 300) && !_wcsicmp(have, full);
    if (f) RegCloseKey(f);
    if (mine) {
        swprintf(k, 700, L"%ls\\Families\\%ls", MACHINE_REPO, fam);
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, k);
    }
}

/* the installed packages that depend on the family @family: their full
 * names in @out, a count */
static int dependents(const WCHAR *family, WCHAR out[][300], int cap)
{
    WCHAR k[400], full[300], v[300], name[16];
    HKEY h;
    int n = 0;
    swprintf(k, 400, L"%ls\\Packages", MACHINE_REPO);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, k, 0, KEY_READ, &h)) return 0;
    for (DWORD i = 0; n < cap; i++) {
        DWORD len = 300;
        if (RegEnumKeyExW(h, i, full, &len, NULL, NULL, NULL, NULL)) break;
        HKEY d = machine_key(L"Packages", full, L"Dependencies", 0);
        for (DWORD j = 0; d; j++) {
            swprintf(name, 16, L"%lu", (unsigned long)j);
            if (!get_str(d, name, v, 300)) break;
            if (!_wcsicmp(v, family)) { wcscpy(out[n++], full); break; }
        }
        if (d) RegCloseKey(d);
    }
    RegCloseKey(h);
    return n;
}

/* the full name's parts: its architecture */
static void full_arch(const WCHAR *full, WCHAR *out, size_t cap)
{
    const WCHAR *a = wcschr(full, '_');
    a = a ? wcschr(a + 1, '_') : NULL;
    const WCHAR *e = a ? wcschr(a + 1, '_') : NULL;
    out[0] = 0;
    if (a && e && (size_t)(e - a - 1) < cap) { memcpy(out, a + 1, (e - a - 1) * sizeof(WCHAR)); out[e - a - 1] = 0; }
}

/* each of @pk's dependencies found among the installed packages: one of
 * its family, at least its minimum version, for this package's processor
 * (or neutral) */
static int find_dependencies(Package *pk)
{
    for (int i = 0; i < pk->ndeps; i++) {
        Dependency *d = &pk->deps[i];
        WCHAR members[16][300], arch[16];
        int n = family_packages(d->family, members, 16);
        d->full[0] = 0;
        for (int m = 0; m < n && !d->full[0]; m++) {
            const WCHAR *v = wcschr(members[m], '_');
            full_arch(members[m], arch, 16);
            if (v && version_number(v + 1) >= version_number(d->minver) &&
                (!wcscmp(arch, L"neutral") || !wcscmp(arch, pk->arch) || !wcscmp(pk->arch, L"neutral")))
                wcscpy(d->full, members[m]);
        }
        if (!d->full[0]) {
            printf("appx: %ls: needs the framework package %ls %ls or newer (%ls, from %ls), which is not installed: "
                   "install it first\n", pk->full, d->name, d->minver, pk->arch, d->publisher);
            return 0;
        }
    }
    return 1;
}

/* ---- installing ---- */

/* @data (@n bytes, read from @file): a package, or a bundle of packages;
 * @signed_by: the verified bundle it came from (its publisher), if any,
 * when the bundle's block map covers it; @outside: the bundle's publisher
 * when its block map does not (then only its own signature counts) */
static int install_data(const BYTE *data, size_t n, const WCHAR *file, int allow_unsigned, const WCHAR *signed_by,
                        const WCHAR *outside);

static int install_bundle(Package *b, const BYTE *data, size_t n, const WCHAR *file, int allow_unsigned)
{
    Entry *e = zip_find(&b->zip, L"AppxMetadata\\AppxBundleManifest.xml");
    BYTE *m = zip_read(&b->zip, e);
    Node *root = m ? xml_parse(m, (size_t)e->usize) : NULL;
    Node *bundle = child(root, L"Bundle"), *id = child(bundle, L"Identity");
    const WCHAR *publisher = attr(id, L"Publisher");
    if (!id || !attr(id, L"Name") || !publisher) return !fail("%ls: the bundle's manifest has no identity", file);
    if (!check_blockmap(b, bundle)) return 1;
    int sig = zip_find(&b->zip, L"AppxSignature.p7x") != NULL;
    if (sig && !check_signature(data, n, publisher, file)) return 1;
    if (!sig && !allow_unsigned)
        return !fail("%ls: the bundle is not signed (an unsigned bundle installs only with -AllowUnsigned)", file);
    if (!sig && !wcsstr(publisher, UNSIGNED_OID))
        return !fail("%ls: an unsigned bundle's publisher must carry " UNSIGNED_OID_A " (Windows' rule)", publisher);
    /* the application package for this processor */
    static const WCHAR *const order[] = { L"x64", L"neutral", L"x86" };
    Node *pick = NULL;
    for (int o = 0; o < 3 && !pick; o++)
        for (Node *p = child(child(bundle, L"Packages"), L"Package"); p && !pick; p = sibling(p, L"Package")) {
            const WCHAR *type = attr(p, L"Type"), *arch = attr(p, L"Architecture");
            if ((!type || !_wcsicmp(type, L"application")) && arch && !_wcsicmp(arch, order[o])) pick = p;
        }
    if (!pick) return !fail("%ls: the bundle has no application package for this processor (x64, neutral, x86)", file);
    for (Node *p = child(child(bundle, L"Packages"), L"Package"); p; p = sibling(p, L"Package"))
        if (attr(p, L"Type") && !_wcsicmp(attr(p, L"Type"), L"resource"))
            printf("appx: %ls: a resource package, not installed (resource packages are not installed yet)\n",
                   attr(p, L"FileName") ? attr(p, L"FileName") : L"(no name)");
    WCHAR name[MAX_PATH];
    const WCHAR *fn = attr(pick, L"FileName");
    if (!fn || wcslen(fn) >= MAX_PATH) return !fail("%ls: the bundle names a package without a file", file);
    wcscpy(name, fn);
    for (WCHAR *c = name; *c; c++) if (*c == '/') *c = '\\';
    Entry *inner = zip_find(&b->zip, name);
    BYTE *bytes = inner ? zip_read(&b->zip, inner) : NULL;
    if (!bytes) return !fail("%ls: in the bundle's manifest but not in the bundle", name);
    printf("appx: %ls: installing %ls from the bundle\n", file, name);
    if (!inner->covered)
        return install_data(bytes, (size_t)inner->usize, name, allow_unsigned, NULL, publisher);
    return install_data(bytes, (size_t)inner->usize, name, allow_unsigned, sig ? publisher : NULL, NULL);
}

static int install_data(const BYTE *data, size_t size, const WCHAR *file, int allow_unsigned, const WCHAR *signed_by,
                        const WCHAR *outside)
{
    Package pk;
    memset(&pk, 0, sizeof pk);
    if (!zip_open(&pk.zip, data, (unsigned long long)size)) return !fail("%ls: not a package (not a zip)", file);
    if (!zip_find(&pk.zip, L"AppxManifest.xml") && zip_find(&pk.zip, L"AppxMetadata\\AppxBundleManifest.xml")) {
        if (signed_by || outside) return !fail("%ls: a bundle inside a bundle", file);
        return install_bundle(&pk, data, size, file, allow_unsigned);
    }
    if (!read_manifest(&pk) || !check_blockmap(&pk, NULL)) return 1;

    /* the signature: verified, or (unsigned) allowed as Windows allows it;
     * inside a signed bundle whose block map covers it, the bundle's covers
     * it; outside the bundle's block map (the Store's bundles), only its
     * own; a bundle's package is its bundle's publisher's, as Windows
     * requires */
    const WCHAR *bundle_pub = signed_by ? signed_by : outside;
    if (bundle_pub) {
        WCHAR a[600], b[600];
        name_key(bundle_pub, a, 600);
        name_key(pk.publisher, b, 600);
        if (wcscmp(a, b)) return !fail("%ls: its publisher is not its bundle's", pk.full);
    }
    if (zip_find(&pk.zip, L"AppxSignature.p7x")) {
        if (!check_signature(data, size, pk.publisher, pk.full)) return 1;
    } else if (outside) {
        return !fail("%ls: outside its bundle's block map and not signed itself, so it is not installed", pk.full);
    } else if (!signed_by) {
        if (!allow_unsigned)
            return !fail("%ls: the package is not signed (an unsigned package installs only with -AllowUnsigned)", pk.full);
        if (!wcsstr(pk.publisher, UNSIGNED_OID))
            return !fail("%ls: an unsigned package's publisher must carry " UNSIGNED_OID_A " (Windows' rule)", pk.publisher);
    }
    if (!find_dependencies(&pk)) return 1;

    /* the family's other versions: an older one is replaced, a newer one kept */
    WCHAR others[16][300];
    int nothers = family_packages(pk.family, others, 16);
    for (int i = 0; i < nothers; i++) {
        const WCHAR *v = wcschr(others[i], '_');
        if (v && version_number(v + 1) > version_number(pk.version))
            return !fail("%ls: a newer version of this package is installed", others[i]);
    }

    WCHAR root[MAX_PATH], path[MAX_PATH * 2];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES_, NULL, 0, root))) wcscpy(root, L"C:\\Programs");
    wcscat(root, L"\\WindowsApps");
    CreateDirectoryW(root, NULL);
    swprintf(root + wcslen(root), MAX_PATH - wcslen(root), L"\\%ls", pk.full);
    /* (Windows' deployment service writes WindowsApps as the system for
     * any user; NovaOS has none yet, so the installer must be elevated) */
    if (!CreateDirectoryW(root, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return !fail(GetLastError() == ERROR_ACCESS_DENIED ?
                     "%ls: installing a package needs an administrator (sudo appx install ...)" :
                     "%ls: cannot be created", root);
    for (int i = 0; i < nothers; i++) remove_package(others[i]);   /* (the same version too: reinstalled) */
    for (int i = 0; i < pk.zip.count; i++) {
        Entry *e = &pk.zip.e[i];
        size_t n = wcslen(e->name);
        if (n && e->name[n - 1] == '\\') continue;                  /* (a folder) */
        if (!safe_name(e->name)) return !fail("%ls: a file name outside the package's folder", e->name);
        BYTE *bytes = zip_read(&pk.zip, e);
        swprintf(path, MAX_PATH * 2, L"%ls\\%ls", root, e->name);
        if (!bytes || !write_file(path, bytes, (size_t)e->usize)) {
            delete_tree(root);
            printf("appx: error %lu: ", (unsigned long)GetLastError());
            return !fail("%ls: cannot be written", path);
        }
    }

    HKEY pkey = repo_key(L"Packages\\%ls%ls", pk.full, L"", 1), k;
    if (!pkey) { delete_tree(root); return !fail("%ls: cannot be recorded in the package repository", pk.full); }
    set_str(pkey, L"PackageRootFolder", root);
    set_str(pkey, L"Publisher", pk.publisher);
    set_str(pkey, L"Version", pk.version);
    if ((k = repo_key(L"Families\\%ls\\%ls", pk.family, pk.full, 1))) RegCloseKey(k);
    WCHAR display[512], pubdisplay[512];
    resolve(pk.full, pk.display, display, 512);                     /* (now the package is known) */
    resolve(pk.full, pk.pubdisplay, pubdisplay, 512);
    set_str(pkey, L"DisplayName", display);
    set_str(pkey, L"PublisherDisplayName", pubdisplay);
    set_dword(pkey, L"Framework", (DWORD)pk.framework);
    RegCloseKey(pkey);
    /* machine-wide: the package, its dependencies' families, its family's member */
    HKEY mk = machine_key(L"Packages", pk.full, NULL, 1), fk = machine_key(L"Families", pk.family, NULL, 1);
    if (!mk || !fk) {
        if (mk) RegCloseKey(mk);
        if (fk) RegCloseKey(fk);
        remove_package(pk.full);
        return !fail("%ls: cannot be recorded in the machine's package repository (HKLM)", pk.full);
    }
    set_str(mk, L"Path", root);
    set_dword(mk, L"Framework", (DWORD)pk.framework);
    RegCloseKey(mk);
    set_str(fk, L"FullName", pk.full);
    set_str(fk, L"Path", root);
    RegCloseKey(fk);
    if (pk.ndeps && (mk = machine_key(L"Packages", pk.full, L"Dependencies", 1))) {
        for (int i = 0; i < pk.ndeps; i++) {
            WCHAR name[16];
            swprintf(name, 16, L"%d", i);
            set_str(mk, name, pk.deps[i].family);
        }
        RegCloseKey(mk);
    }
    for (int i = 0; i < pk.napps; i++) {
        App *a = &pk.apps[i];
        WCHAR name[512], exe[MAX_PATH * 2], lnk[MAX_PATH] = L"";
        if (!safe_name(a->exe)) { remove_package(pk.full); return !fail("%ls: a program outside the package", a->exe); }
        resolve(pk.full, a->display, name, 512);
        swprintf(exe, MAX_PATH * 2, L"%ls\\%ls", root, a->exe);
        if (_wcsicmp(a->show, L"none") && !start_menu_link(exe, root, name, display, lnk))
            printf("appx: %ls: no Start menu entry\n", name);
        if (!(k = repo_key(L"Packages\\%ls\\Applications\\%ls", pk.full, a->id, 1))) continue;
        set_str(k, L"Executable", a->exe);
        set_str(k, L"EntryPoint", a->entry);
        set_str(k, L"RuntimeBehavior", a->behavior);
        set_str(k, L"DisplayName", name);
        set_str(k, L"StartMenuLink", lnk);
        RegCloseKey(k);
    }
    for (int i = 0; i < pk.nservers; i++) {
        Server *s = &pk.servers[i];
        WCHAR dll[MAX_PATH * 2], key[700];
        if (!safe_name(s->dll)) continue;
        swprintf(dll, MAX_PATH * 2, L"%ls\\%ls", root, s->dll);
        for (int c = 0; c < s->ncls; c++) {
            swprintf(key, 700, L"%ls\\%ls\\ActivatableClassId\\%ls", CLASSES_KEY, pk.full, s->cls[c]);
            if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL)) continue;
            set_str(k, L"DllPath", dll);
            set_dword(k, L"ActivationType", 0);                      /* in process */
            set_dword(k, L"Threading", !_wcsicmp(s->threading[c], L"STA") ? 1 : !_wcsicmp(s->threading[c], L"MTA") ? 2 : 0);
            RegCloseKey(k);
        }
    }
    printf("Installed %ls (%ls)%ls\n", pk.full, display, pk.framework ? L", a framework package" : L"");
    for (int i = 0; i < pk.ndeps; i++) printf("  using the framework %ls\n", pk.deps[i].full);
    for (int i = 0; i < pk.napps; i++) printf("  start it with: appx start %ls!%ls\n", pk.family, pk.apps[i].id);
    return 0;
}

static int install(const WCHAR *file, int allow_unsigned)
{
    HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER size;
    if (h == INVALID_HANDLE_VALUE || !GetFileSizeEx(h, &size) || size.QuadPart > 0x7FFFFFFF) {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        return !fail("%ls: cannot be read", file);
    }
    BYTE *data = xmalloc((size_t)size.QuadPart);
    DWORD got = 0;
    BOOL ok = ReadFile(h, data, (DWORD)size.QuadPart, &got, NULL) && got == size.QuadPart;
    CloseHandle(h);
    if (!ok) return !fail("%ls: cannot be read", file);
    return install_data(data, (size_t)size.QuadPart, file, allow_unsigned, NULL, NULL);
}

/* each installed package: the callback gets its full name and its key */
static void each_package(void (*fn)(const WCHAR *full, HKEY k, void *ctx), void *ctx)
{
    HKEY h = repo_key(L"Packages%ls%ls", L"", L"", 0);
    WCHAR full[300];
    if (!h) return;
    for (DWORD i = 0; ; i++) {
        DWORD n = 300;
        HKEY k;
        if (RegEnumKeyExW(h, i, full, &n, NULL, NULL, NULL, NULL)) break;
        if (!RegOpenKeyExW(h, full, 0, KEY_READ, &k)) {
            fn(full, k, ctx);
            RegCloseKey(k);
        }
    }
    RegCloseKey(h);
}

static void list_one(const WCHAR *full, HKEY k, void *ctx)
{
    WCHAR v[MAX_PATH];
    (void)ctx;
    printf("%ls\n", full);
    if (get_str(k, L"DisplayName", v, MAX_PATH)) printf("  Name:     %ls\n", v);
    if (get_str(k, L"PublisherDisplayName", v, MAX_PATH)) printf("  Publisher: %ls\n", v);
    if (get_str(k, L"PackageRootFolder", v, MAX_PATH)) printf("  Folder:   %ls\n", v);
}

/* @what names the package: its full name, its family or its name */
static int names(const WCHAR *full, const WCHAR *what)
{
    size_t n = wcslen(what);
    const WCHAR *us = wcschr(full, '_'), *last = wcsrchr(full, '_');
    if (!_wcsicmp(full, what)) return 1;
    if (us && (size_t)(us - full) == n && !_wcsnicmp(full, what, n)) return 1;   /* Name */
    if (us && last && n == (size_t)(us - full) + wcslen(last) && !_wcsnicmp(full, what, us - full) &&
        !_wcsicmp(what + (us - full), last))
        return 1;                                                                /* Name_PublisherId */
    return 0;
}

typedef struct { const WCHAR *what; WCHAR found[16][300]; int n; } Match;
static void match_one(const WCHAR *full, HKEY k, void *ctx)
{
    Match *m = ctx;
    (void)k;
    if (m->n < 16 && names(full, m->what)) wcscpy(m->found[m->n++], full);
}

static int remove_cmd(const WCHAR *what)
{
    Match m = { what };
    each_package(match_one, &m);
    if (!m.n) return !fail("%ls: no such package is installed", what);
    for (int i = 0; i < m.n; i++) {
        WCHAR fam[300], users[4][300];
        const WCHAR *us = wcschr(m.found[i], '_'), *last = wcsrchr(m.found[i], '_');
        swprintf(fam, 300, L"%.*ls%ls", (int)(us - m.found[i]), m.found[i], last);
        if (dependents(fam, users, 4)) {
            printf("appx: %ls: a framework that %ls uses: remove that first\n", m.found[i], users[0]);
            return 1;
        }
        if (!remove_package(m.found[i]))
            return !fail("%ls: unregistered, but its folder could not be deleted (are its files in use?)", m.found[i]);
        printf("Removed %ls\n", m.found[i]);
    }
    return 0;
}

static int start_cmd(const WCHAR *what, const WCHAR *args, int wait)
{
    WCHAR aumid[400];
    if (wcschr(what, '!')) wcsncpy(aumid, what, 399), aumid[399] = 0;
    else {                                         /* a package: its first application */
        Match m = { what };
        each_package(match_one, &m);
        if (!m.n) return !fail("%ls: no such package is installed", what);
        HKEY apps = repo_key(L"Packages\\%ls\\Applications%ls", m.found[0], L"", 0);
        WCHAR id[128];
        DWORD n = 128;
        if (!apps || RegEnumKeyExW(apps, 0, id, &n, NULL, NULL, NULL, NULL)) {
            if (apps) RegCloseKey(apps);
            return !fail("%ls: the package has no application", m.found[0]);
        }
        RegCloseKey(apps);
        const WCHAR *us = wcschr(m.found[0], '_'), *last = wcsrchr(m.found[0], '_');
        swprintf(aumid, 400, L"%.*ls%ls!%ls", (int)(us - m.found[0]), m.found[0], last, id);
    }
    typedef HRESULT (STDMETHODCALLTYPE *Activate)(void *, LPCWSTR, LPCWSTR, int, DWORD *);
    void *am = NULL;
    DWORD pid = 0;
    HRESULT hr = CoCreateInstance(&CLSID_ApplicationActivationManager_, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IApplicationActivationManager_, &am);
    if (SUCCEEDED(hr)) {
        hr = ((Activate)VT(am)[3])(am, aumid, args, 0, &pid);
        ((IUnknown *)am)->lpVtbl->Release(am);
    }
    if (FAILED(hr)) {
        printf("appx: %ls: cannot be started (0x%08lx)\n", aumid, (unsigned long)hr);
        return 1;
    }
    printf("Started %ls (process %lu)\n", aumid, (unsigned long)pid);
    if (!wait) return 0;
    HANDLE p = OpenProcess(SYNCHRONIZE | 0x1000 /* PROCESS_QUERY_LIMITED_INFORMATION */, FALSE, pid);
    DWORD code = 1;
    if (!p) return !fail("%ls: cannot wait for it", aumid);
    WaitForSingleObject(p, INFINITE);
    GetExitCodeProcess(p, &code);
    CloseHandle(p);
    printf("%ls exited with %lu\n", aumid, (unsigned long)code);
    return (int)code;
}

static int usage(void)
{
    printf("usage: appx [install] PACKAGE.msix [-AllowUnsigned]\n"
           "       appx list\n"
           "       appx remove NAME | FAMILY | FULLNAME\n"
           "       appx start AUMID | NAME [ARGUMENTS...] [-Wait]\n");
    return 2;
}

int main(void)
{
    int argc;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 2) return usage();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    const WCHAR *cmd = argv[1];
    int r;
    if (!_wcsicmp(cmd, L"list") && argc == 2) {
        each_package(list_one, NULL);
        r = 0;
    } else if (!_wcsicmp(cmd, L"remove") && argc == 3)
        r = remove_cmd(argv[2]);
    else if (!_wcsicmp(cmd, L"start") && argc >= 3) {
        WCHAR args[2048] = L"";
        int wait = 0;
        size_t at = 0;
        for (int i = 3; i < argc; i++) {
            if (!_wcsicmp(argv[i], L"-Wait")) { wait = 1; continue; }
            int quote = !argv[i][0] || wcschr(argv[i], ' ');
            at += swprintf(args + at, 2048 - at, quote ? L"%ls\"%ls\"" : L"%ls%ls", at ? L" " : L"", argv[i]);
            if (at >= 2000) return usage();
        }
        r = start_cmd(argv[2], args, wait);
    } else {
        int i = !_wcsicmp(cmd, L"install") ? 2 : 1, allow = 0;
        const WCHAR *file = NULL;
        for (; i < argc; i++) {
            if (!_wcsicmp(argv[i], L"-AllowUnsigned")) allow = 1;
            else if (!file) file = argv[i];
            else return usage();
        }
        r = file ? install(file, allow) : usage();
    }
    CoUninitialize();
    return r;
}
