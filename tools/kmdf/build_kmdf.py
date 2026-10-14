#!/usr/bin/env python3
"""build_kmdf.py — builds Wdf01000.sys, KMDF, from Microsoft's own source

    build_kmdf.py OUTDIR [--src DIR] [--keep-going] [--jobs N]

KMDF is Microsoft's Windows Driver Frameworks, published under the MIT
licence at github.com/microsoft/Windows-Driver-Frameworks.  NovaOS runs that
code unchanged but for the patches in tools/kmdf/patches (each says what
it restores and why): this script fetches the pinned commit (WDF_COMMIT; a
git commit id names its content, so the fetch is verified by git itself)
into ~/.cache/novaos/wdf, applies the patches, compiles the kernel-mode
framework (framework/kmdf and the kernel-mode half of framework/shared)
with clang, and links Wdf01000.sys with lld-link /driver against import
libraries for ntoskrnl.exe, hal.dll (kernel/nt/exports.h) and WdfLdr.sys.
It also links WdfLdr.sys, the KMDF loader, and two KMDF drivers built as
the WDK builds one (the client stub tools/kmdf/stub.c, which binds them to
the framework): kmdftest.sys (tools/drivertest/kmdftest.c) and kmdfedu.sys,
the Plug and Play driver for QEMU's edu device (tools/drivertest/kmdfedu.c).
And it builds a third-party KMDF driver unchanged, as its vendor's build
would: vhidmini.sys, Microsoft's virtual HID minidriver sample (vhidmini2,
KMDF version), from the pinned commit of github.com/microsoft/
Windows-driver-samples (VHID_COMMIT, MS-PL), with its INF stamped as the
WDK's stampinf does (the .inx's $ARCH$ and $KMDFVERSION$) and a catalog of
the package named as the INF names it (wudf.cat).
It test-signs the first three with tools/drivertest/mksys.py's certificate;
kmdfedu.sys comes as a package, as edutest.sys does: kmdfedu.inf and a
test-signed catalog of both.  OUTDIR gets those files; the result is kept in ~/.cache/novaos/wdf/out-<hash of the
inputs> and copied from there while nothing it is made from changed.

The published source is the framework only, not the build around it: the
WDK headers it was built with, a few framework-private headers, the WPP
trace headers (.tmh) the WDK's preprocessor writes, the WDK's static
libraries (wdmsec.lib, the USB and aux_klib helpers) and the loader
(WdfLdr.sys, with the WdfBindClientHelper routine the framework links
from it).  NovaOS supplies those:

  * the WDK headers: mingw-w64's (public domain) under tools/kmdf/inc,
    which adds what they lack (tools/kmdf/inc/README.md lists each file);
  * the framework-private headers (wdfldr.h, wdfcx.h, ...): NovaOS's own,
    written from the declarations the source uses, in tools/kmdf/inc;
  * the .tmh files: NovaOS's trace macros (tools/kmdf/inc/novaos_wpp.h:
    warnings and errors go to DbgPrintEx);
  * the libraries and WdfBindClientHelper: tools/kmdf/rt;
  * WdfLdr.sys: NovaOS's own (tools/kmdf/wdfldr.c).

The source tree is case-insensitive on Windows and includes "FxDevice.tmh"
for fxdevice.tmh; an overlay of links under OUTDIR/obj/case gives every
spelling the source uses.
"""
import argparse, concurrent.futures, hashlib, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'drivertest'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'authenticode'))
from mksys import import_libs, signed_package, test_signer                     # noqa: E402
from catalog import catalog                                     # noqa: E402
from sign import sign                                           # noqa: E402

WDF_REPO = 'https://github.com/microsoft/Windows-Driver-Frameworks.git'
WDF_COMMIT = 'b6191d9543441329154da32f7ab9bdd97228dd3c'
KMDF_HEADERS = '1.33'          # publicinc/wdf/kmdf/<this>: the DDI list the source implements
SAMPLES_REPO = 'https://github.com/microsoft/Windows-driver-samples.git'
VHID_COMMIT = '2dc3fd3a0cc84a2933f2194e7ec0871584979071'
VHID_DIR = 'hid/vhidmini2'     # (with the samples' LICENSE, all that is fetched)
MINGW = '/usr/share/mingw-w64/include'

# mingw-w64's wdm.h follows the Windows 7 WDK.  Where the framework needs a
# later version of a structure it already defines (adding fields cannot be
# done from another header), build_kmdf.py compiles against a copy of it
# with that definition replaced: (what, pattern, replacement), each pattern
# matching exactly once.
WDM_EDITS = [
    ('DEVICE_DESCRIPTION version 3',
     r'typedef struct _DEVICE_DESCRIPTION \{.*?\} DEVICE_DESCRIPTION, \*PDEVICE_DESCRIPTION;',
     '#include "novaos_device_description.h"'),
    ('DMA_OPERATIONS version 3',
     r'typedef struct _DMA_OPERATIONS \{.*?\} DMA_OPERATIONS, \*PDMA_OPERATIONS;',
     '#include "novaos_dma_operations.h"'),
    ('DEVICE_CAPABILITIES: the Windows 8 flags (WakeFromInterrupt, ...)',
     r'ULONG NoDisplayInUI:1;\s*ULONG Reserved:14;',
     'ULONG NoDisplayInUI:1;\n  ULONG Reserved1:1;\n  ULONG WakeFromInterrupt:1;\n  ULONG SecureDevice:1;\n'
     '  ULONG ChildOfVgaEnabledBridge:1;\n  ULONG DecodeIoOnBoot:1;\n  ULONG Reserved:9;'),
    ('IO_RESOURCE_DESCRIPTOR: interrupt affinity policy and group (Windows 7)',
     r'ULONG MinimumVector;\s*ULONG MaximumVector;\s*\} Interrupt;',
     'ULONG MinimumVector;\n      ULONG MaximumVector;\n      IRQ_DEVICE_POLICY AffinityPolicy;\n      USHORT Group;\n'
     '      IRQ_PRIORITY PriorityPolicy;\n      KAFFINITY TargetedProcessors;\n    } Interrupt;'),
    ('MmMapLockedPagesSpecifyCache takes MdlMapping* flags in Priority',
     r'(MmMapLockedPagesSpecifyCache\([^;]*?)IN MM_PAGE_PRIORITY Priority\);',
     r'\1IN ULONG Priority);'),
]


# What WdfLdr.sys exports (wdfldr.c; fxldr.h declares them)
WDFLDR_EXPORTS = ['WdfLdrDiagnosticsValueByNameAsULONG', 'WdfLdrQueryInterface', 'WdfRegisterClassLibrary',
                  'WdfRegisterLibrary', 'WdfVersionBind', 'WdfVersionBindClass', 'WdfVersionUnbind',
                  'WdfVersionUnbindClass', 'DllInitialize', 'DllUnload']


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        line = ' '.join(cmd)
        sys.stderr.write((line if len(line) < 2000 else line[:2000] + ' ...') + '\n' + r.stdout + r.stderr)
        sys.exit(1)
    return r.stdout


def fetch(cache, repo=WDF_REPO, commit=WDF_COMMIT, only=None):
    """The pinned commit's tree (only the paths @only of it, when given),
    fetched once into @cache"""
    src = os.path.join(cache, commit)
    if os.path.isfile(os.path.join(src, '.complete')) and all(os.path.exists(os.path.join(src, *p.split('/')))
                                                               for p in only or ()):
        return src
    if os.path.isdir(src):
        shutil.rmtree(src)
    os.makedirs(src)
    run(['git', 'init', '-q', src])
    if only:
        run(['git', '-C', src, 'config', 'core.sparseCheckout', 'true'])
        with open(os.path.join(src, '.git', 'info', 'sparse-checkout'), 'w') as f:
            f.write(''.join('/' + p + '\n' for p in only))
    for attempt in range(4):
        r = subprocess.run(['git', '-C', src, 'fetch', '-q', '--depth', '1', repo, commit],
                           capture_output=True, text=True)
        if not r.returncode:
            break
        sys.stderr.write(r.stderr)
    else:
        sys.exit('build_kmdf: cannot fetch ' + repo)
    run(['git', '-C', src, 'checkout', '-q', commit])
    open(os.path.join(src, '.complete'), 'w').close()
    return src


def stamp_inf(inx, out):
    """What the WDK's stampinf does to a driver's .inx: the architecture
    and KMDF version filled in (the file stays UTF-16, as written)"""
    raw = open(inx, 'rb').read()
    enc = 'utf-16' if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else 'utf-8-sig' if raw[:3] == b'\xef\xbb\xbf' else 'latin-1'
    text = raw.decode(enc).replace('$ARCH$', 'amd64').replace('$KMDFVERSION$', KMDF_HEADERS)
    with open(out, 'wb') as f:
        f.write(text.encode('utf-16') if enc == 'utf-16' else text.encode('utf-8-sig' if enc == 'utf-8-sig' else 'latin-1'))
    m = re.search(r'^\s*CatalogFile\s*=\s*(\S+)', text, re.M | re.I)
    return m.group(1) if m else None


def sources(fw):
    """The kernel-mode framework: every .c/.cpp under framework/kmdf and
    framework/shared except the user-mode (um) halves"""
    out = []
    for top in ('kmdf', 'shared'):
        for d, _, files in os.walk(os.path.join(fw, top)):
            if 'um' in os.path.relpath(d, fw).split(os.sep):
                continue
            out += [os.path.join(d, f) for f in files if f.endswith(('.c', '.cpp'))]
    return sorted(out)


def header_dirs(fw, pub):
    """Every directory holding the framework's headers (the WDK build gave
    each library its own include list; one list for all is the same set)"""
    dirs = [pub]
    for top in ('kmdf', 'shared'):
        for d, _, files in os.walk(os.path.join(fw, top)):
            if 'um' in os.path.relpath(d, fw).split(os.sep):
                continue
            if any(f.endswith(('.h', '.hpp')) for f in files):
                dirs.append(d)
    return sorted(set(dirs), key=dirs.index)


def case_overlay(case, files, dirs):
    """Links so each #include spelling finds its file: the source was
    written for a case-insensitive file system"""
    os.makedirs(case, exist_ok=True)
    index = {}
    for d in dirs:
        for f in os.listdir(d):
            index.setdefault(f.lower(), os.path.join(d, f))
    wanted = set()
    inc = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
    for path in files:
        try:
            wanted.update(inc.findall(open(path, encoding='latin-1').read()))
        except OSError:
            pass
    for name in sorted(wanted):
        if '/' in name:
            continue
        low = name.replace('\\', '/').split('/')[-1].lower()     # ("km\\FxSupportPchKM.hpp": a literal name here)
        link = os.path.join(case, name)
        if os.path.lexists(link):
            continue
        if low.endswith('.tmh'):
            with open(link, 'w') as f:              # (WPP's output: NovaOS's trace macros instead)
                f.write('#include "novaos_wpp.h"\n')
            continue
        target = index.get(low)
        if target and '\\' in name:                # (clang reads the backslash as a separator)
            sub = os.path.join(case, *name.split('\\'))
            os.makedirs(os.path.dirname(sub), exist_ok=True)
            if not os.path.lexists(sub):
                os.symlink(target, sub)
        elif target and os.path.basename(target) != name:
            os.symlink(target, link)


def struct_list(pub, dest):
    """gen/wdfstructs.inc: WDF_STRUCT(name) for each public structure, in
    WDFSTRUCTENUM order (the INDEX_ names of wdffuncenum.h), for the
    structure-size table WdfBindClientHelper hands newer clients
    (rt/bindhelper.cpp)"""
    text = open(os.path.join(pub, 'wdffuncenum.h')).read()
    names = sorted((int(i), n) for n, i in re.findall(r'^\s*INDEX_(\w+)\s*=\s*(\d+),', text, re.M))
    if [i for i, _ in names] != list(range(len(names))):
        sys.exit('build_kmdf: wdffuncenum.h: WDFSTRUCTENUM is not 0..n-1')
    os.makedirs(dest, exist_ok=True)
    with open(os.path.join(dest, 'wdfstructs.inc'), 'w') as f:
        f.write(''.join(f'WDF_STRUCT({n})\n' for _, n in names))


def mingw_wdm(dest):
    """mingw-w64's wdm.h with WDM_EDITS applied, written under @dest"""
    text = open(os.path.join(MINGW, 'ddk', 'wdm.h'), encoding='latin-1').read()
    for what, pattern, repl in WDM_EDITS:
        text, n = re.subn(pattern, repl, text, flags=re.S)
        if n != 1:
            sys.exit(f'build_kmdf: mingw-w64 wdm.h: {what}: {n} matches (expected 1)')
    os.makedirs(dest, exist_ok=True)
    with open(os.path.join(dest, 'wdm.h'), 'w', encoding='latin-1') as f:
        f.write(text)


def patched_tree(src, dest):
    """A copy of the source with tools/kmdf/patches/*.patch applied: the
    lines Microsoft blanked out of the published files (each patch says
    which and why)"""
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    for sub in ('framework', 'publicinc'):
        shutil.copytree(os.path.join(src, 'src', sub), os.path.join(dest, 'src', sub), symlinks=True)
    # (a repository of its own: inside another one, such as NovaOS's build
    # directory, git apply would skip every path outside that one's index)
    run(['git', 'init', '-q', dest])
    pdir = os.path.join(HERE, 'patches')
    for name in sorted(os.listdir(pdir)):
        if name.endswith('.patch'):
            run(['git', 'apply', '--whitespace=nowarn', os.path.join(pdir, name)], cwd=dest)
    return dest


OUTPUTS = ('Wdf01000.sys', 'WdfLdr.sys', 'kmdftest.sys', 'kmdfedu.sys', 'kmdfedu.inf', 'kmdfedu.cat',
           'vhidmini/vhidmini.sys', 'vhidmini/vhidmini.inf', 'vhidmini/wudf.cat', 'vhidmini/LICENSE.txt')
CACHE = os.path.join(os.path.expanduser('~'), '.cache', 'novaos', 'wdf')


def inputs_hash():
    """A hash of everything the outputs are made from (with the compiler's version)"""
    h = hashlib.sha256((WDF_COMMIT + VHID_COMMIT).encode())
    files = [os.path.join(d, n) for d, _, ns in os.walk(HERE) if '__pycache__' not in d for n in ns]
    files += [os.path.join(ROOT, *p.split('/')) for p in (
        'tools/drivertest/kmdftest.c', 'tools/drivertest/kmdftest.h', 'tools/drivertest/mksys.py',
        'tools/drivertest/kmdfedu.c', 'tools/drivertest/kmdfedu.inf', 'tools/drivertest/edutest.h',
        'kernel/nt/exports.h', 'tools/authenticode/sign.py', 'tools/authenticode/catalog.py')]
    for path in sorted(files):
        h.update(os.path.relpath(path, ROOT).encode() + b'\0' + open(path, 'rb').read())
    for tool in ('clang', 'lld-link'):
        h.update(subprocess.run([tool, '--version'], capture_output=True).stdout)
    return h.hexdigest()[:16]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--src', help='a patched checkout of Windows-Driver-Frameworks (default: fetch the pinned commit and apply tools/kmdf/patches)')
    ap.add_argument('--keep-going', action='store_true', help='compile every file and list each failure')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 2)
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    kept = None if args.src else os.path.join(CACHE, 'out-' + inputs_hash())
    if kept and all(os.path.isfile(os.path.join(kept, n)) for n in OUTPUTS):
        for n in OUTPUTS:
            os.makedirs(os.path.dirname(os.path.join(out, n)), exist_ok=True)
            shutil.copy(os.path.join(kept, n), os.path.join(out, n))
        print(f'build_kmdf: {", ".join(OUTPUTS)} unchanged (from {kept})')
        return
    work = os.path.join(out, 'obj')
    os.makedirs(work, exist_ok=True)
    src = args.src or patched_tree(fetch(os.path.join(os.path.expanduser('~'), '.cache', 'novaos', 'wdf')),
                                   os.path.join(work, 'src'))
    fw = os.path.join(src, 'src', 'framework')
    pub = os.path.join(src, 'src', 'publicinc', 'wdf', 'kmdf', KMDF_HEADERS)
    case = os.path.join(work, 'case')
    if os.path.isdir(case):
        shutil.rmtree(case)

    files = sources(fw)
    dirs = header_dirs(fw, pub)
    allfiles = files + [os.path.join(d, f) for d in dirs for f in os.listdir(d)] + \
        [os.path.join(HERE, 'inc', f) for f in os.listdir(os.path.join(HERE, 'inc'))]
    case_overlay(case, allfiles, dirs + [os.path.join(HERE, 'inc'), os.path.join(MINGW, 'ddk'), MINGW])
    # (the framework's triage-info layout: published with UMDF's headers, the same for KMDF)
    os.symlink(os.path.join(src, 'src', 'publicinc', 'wdf', 'umdf', '2.33', 'wdftriage.h'), os.path.join(case, 'wdftriage.h'))
    # (types shared by the kernel- and user-mode device code, kept with the user-mode headers)
    os.symlink(os.path.join(fw, 'shared', 'inc', 'private', 'um', 'device_common.h'), os.path.join(case, 'device_common.h'))
    mingw_wdm(os.path.join(work, 'ddk'))

    resdir = subprocess.run(['clang', '-print-resource-dir'], capture_output=True, text=True).stdout.strip()
    common = ['--target=x86_64-pc-windows-msvc', '-O2', '-ffreestanding', '-fno-stack-protector',
              '-mno-stack-arg-probe', '-fms-extensions', '-fgnuc-version=13.2', '-fno-strict-aliasing',
              '-nostdinc', '-w', '-Wno-invalid-token-paste', '-ferror-limit=0',
              '-isystem', os.path.join(resdir, 'include'),
              '-I', os.path.join(HERE, 'inc'), '-I', case] + \
             sum((['-I', d] for d in dirs), []) + \
             ['-isystem', os.path.join(work, 'ddk'), '-isystem', os.path.join(MINGW, 'ddk'), '-isystem', MINGW,
              '-include', os.path.join(HERE, 'inc', 'novaos_kmdf.h')]
    cxx = ['-std=c++17', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics', '-fno-sized-deallocation',
           '-fms-compatibility']

    gen = os.path.join(work, 'gen')
    struct_list(pub, gen)
    common += ['-I', gen]

    def compile_one(job):
        path, obj = job
        # -fasync-exceptions (MSVC's /EHa) for the C files, whose try/except
        # blocks guard the probes of user buffers: without it the optimizer
        # may assume a memory access inside __try never faults and drop the
        # handler.  (No C++ file has a __try; clang 18 hangs or crashes on
        # two of them with the option.)
        eh = [] if path.endswith('.cpp') else ['-fasync-exceptions']
        cmd = ['clang++' if path.endswith('.cpp') else 'clang'] + common + eh + (cxx if path.endswith('.cpp') else []) + \
              ['-c', path, '-o', obj]
        r = subprocess.run(cmd, capture_output=True, text=True)
        return path, obj, r.returncode, r.stderr

    def compile_all(paths, base):
        jobs = [(p, os.path.join(work, os.path.relpath(p, base).replace(os.sep, '_') + '.o')) for p in paths]
        objs, failed = [], []
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
            for path, obj, rc, err in ex.map(compile_one, jobs):
                if rc:
                    failed.append(path)
                    sys.stderr.write(f'--- {os.path.relpath(path, base)}\n{err}')
                    if not args.keep_going:
                        sys.exit(1)
                objs.append(obj)
        if failed:
            sys.exit(f'build_kmdf: {len(failed)} of {len(paths)} files failed')
        return objs

    fx = compile_all(files, fw)
    rt = compile_all(sorted(os.path.join(HERE, 'rt', n) for n in os.listdir(os.path.join(HERE, 'rt'))
                            if n.endswith(('.c', '.cpp'))), HERE)
    ldr, = compile_all([os.path.join(HERE, 'wdfldr.c')], ROOT)
    # a client driver is built with the WDK's public headers only (and the
    # stub with fxldr.h, the loader interface it calls)
    client = ['--target=x86_64-pc-windows-msvc', '-O2', '-ffreestanding', '-fno-stack-protector',
              '-mno-stack-arg-probe', '-fms-extensions', '-fgnuc-version=13.2', '-fasync-exceptions', '-nostdinc', '-w',
              '-isystem', os.path.join(resdir, 'include'), '-I', os.path.join(HERE, 'inc'), '-I', case, '-I', pub,
              '-I', os.path.join(fw, 'shared', 'inc', 'private', 'common'),
              '-isystem', os.path.join(work, 'ddk'), '-isystem', os.path.join(MINGW, 'ddk'), '-isystem', MINGW,
              '-include', os.path.join(HERE, 'inc', 'novaos_client.h')]
    stub, test, edu = (os.path.join(work, n) for n in ('stub.o', 'kmdftest.o', 'kmdfedu.o'))
    run(['clang'] + client + ['-c', os.path.join(HERE, 'stub.c'), '-o', stub])
    run(['clang'] + client + ['-Wall', '-c', os.path.join(ROOT, 'tools', 'drivertest', 'kmdftest.c'), '-o', test])
    run(['clang'] + client + ['-Wall', '-c', os.path.join(ROOT, 'tools', 'drivertest', 'kmdfedu.c'), '-o', edu])
    # Microsoft's vhidmini2 sample, built as its project does (KMDF, kernel mode)
    vsrc = os.path.join(fetch(os.path.join(os.path.expanduser('~'), '.cache', 'novaos', 'samples'), SAMPLES_REPO,
                              VHID_COMMIT, (VHID_DIR, 'LICENSE')), *VHID_DIR.split('/'))
    vobjs = []
    for f in ('driver/vhidmini.c', 'driver/kmdf/util.c'):
        o = os.path.join(work, 'vhid_' + os.path.basename(f) + '.o')
        run(['clang'] + client + ['-D_KERNEL_MODE', '-I', os.path.join(vsrc, 'inc'), '-I', os.path.join(vsrc, 'driver'),
                                  '-c', os.path.join(vsrc, *f.split('/')), '-o', o])
        vobjs.append(o)
    print(f'build_kmdf: compiled {len(fx)} framework files')

    # import libraries: the kernel's (mksys.py, from kernel/nt/exports.h) and WdfLdr.sys's
    libs = import_libs(work)
    wdfldr_def = os.path.join(work, 'wdfldr.def')
    with open(wdfldr_def, 'w') as f:
        f.write('LIBRARY WDFLDR.SYS\nEXPORTS\n' + ''.join(f'    {n}\n' for n in WDFLDR_EXPORTS))
    wdfldr_lib = os.path.join(work, 'wdfldr.lib')
    run(['llvm-lib', f'/def:{wdfldr_def}', '/machine:x64', f'/out:{wdfldr_lib}'])

    def link(name, objs, extra):
        run(['lld-link', '/driver', '/subsystem:native', '/nodefaultlib', '/machine:x64', '/base:0x140000000',
             '/opt:ref', '/opt:icf', '/errorlimit:0', '/Brepro'] + extra + objs + libs + [f'/out:{os.path.join(out, name)}'])

    link('Wdf01000.sys', fx + rt, ['/entry:DriverEntry', wdfldr_lib])
    link('WdfLdr.sys', [ldr], ['/entry:DriverEntry', f'/def:{wdfldr_def}', f'/implib:{os.path.join(work, "WdfLdr-self.lib")}'])
    # (a client driver links the WDK's wdmsec.lib and has the compiler's
    # CR8 intrinsic: the same stand-ins Wdf01000.sys links)
    client_rt = [o for o in rt if os.path.basename(o) in ('rt_crt.c.o', 'rt_wdmsec.c.o')]
    link('kmdftest.sys', [test, stub] + client_rt, ['/entry:FxDriverEntry', wdfldr_lib])
    link('kmdfedu.sys', [edu, stub] + client_rt, ['/entry:FxDriverEntry', wdfldr_lib])
    vdir = os.path.join(out, 'vhidmini')
    os.makedirs(vdir, exist_ok=True)
    link(os.path.join('vhidmini', 'vhidmini.sys'), vobjs + [stub] + client_rt, ['/entry:FxDriverEntry', wdfldr_lib])
    # (signed by its catalog only: the .sys must be unsigned when the catalog is made)
    signed_package(out, 'kmdfedu')
    cat = stamp_inf(os.path.join(vsrc, 'driver', 'kmdf', 'vhidmini.inx'), os.path.join(vdir, 'vhidmini.inf'))
    if cat != 'wudf.cat':
        sys.exit(f'build_kmdf: vhidmini.inx names catalog {cat}, not wudf.cat (OUTPUTS)')
    signer, root = test_signer()
    files = [(n, open(os.path.join(vdir, n), 'rb').read()) for n in ('vhidmini.inf', 'vhidmini.sys')]
    with open(os.path.join(vdir, cat), 'wb') as f:
        f.write(catalog(files, signer, [root]))
    # (the samples' licence, the MS-PL, goes with the binary)
    shutil.copy(os.path.join(vsrc, '..', '..', 'LICENSE'), os.path.join(vdir, 'LICENSE.txt'))

    # signed as NovaOS's other test drivers are (mksys.py): they load with
    # test signing on (docs/drivers.md says why the framework does too)
    signer, root = test_signer()
    for name in ('Wdf01000.sys', 'WdfLdr.sys', 'kmdftest.sys'):
        path = os.path.join(out, name)
        data = open(path, 'rb').read()
        with open(path, 'wb') as f:
            f.write(sign(data, signer, [root], 'sha256'))
    print(f'build_kmdf: linked Wdf01000.sys, WdfLdr.sys, kmdftest.sys, kmdfedu.sys and vhidmini.sys in {out}')
    if kept:                                       # (the last build only)
        for old in os.listdir(CACHE):
            if old.startswith('out-'):
                shutil.rmtree(os.path.join(CACHE, old), ignore_errors=True)
        os.makedirs(kept)
        for n in OUTPUTS:
            os.makedirs(os.path.dirname(os.path.join(kept, n)), exist_ok=True)
            shutil.copy(os.path.join(out, n), os.path.join(kept, n))


if __name__ == '__main__':
    main()
