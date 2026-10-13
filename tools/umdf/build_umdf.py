#!/usr/bin/env python3
"""build_umdf.py — builds UMDF 2 from Microsoft's own source, and what runs it

    build_umdf.py OUTDIR USERLAND [--src DIR] [--keep-going] [--jobs N]

UMDF 2 is the user-mode half of Microsoft's Windows Driver Frameworks,
published under the MIT licence at github.com/microsoft/Windows-Driver-Frameworks
with KMDF (tools/kmdf/build_kmdf.py, whose fetch of the pinned commit,
patches and helpers this script shares).  It compiles the user-mode
framework (framework/umdf and the user-mode half of framework/shared) with
clang and links Wudfx02000.dll, the framework DLL a UMDF driver runs on,
against NovaOS's own import libraries (USERLAND: build_userland.py's x64
output folder, with crt0.obj and the .lib files).  Around it, NovaOS's own:

  * WUDFRd.sys, the reflector (tools/umdf/wudfrd.c): the kernel-mode
    function driver of every UMDF device, which hands the device's Plug and
    Play and I/O to its driver host;
  * WUDFHost.exe, the driver host (tools/umdf/host): loads the framework
    and the driver, and turns the reflector's messages into requests;
  * umdftest.dll and its package (umdftest.inf, a test-signed catalog
    umdftest.cat), the UMDF test driver for QEMU's edu device
    (tools/drivertest/umdftest.c), linked with NovaOS's client stub
    (tools/umdf/stub.c) as the WDK links a driver with its own.

WUDFRd.sys is test-signed with tools/drivertest/mksys.py's certificate, as
Wdf01000.sys is: it loads with test signing on (docs/drivers.md).  The
headers and pieces Microsoft did not publish (the host interfaces, the
loader interface, the trace headers) are NovaOS's, in tools/umdf/inc
(README.md lists each).  OUTDIR gets Wudfx02000.dll, WUDFHost.exe,
WUDFRd.sys and umdftest\\; the result is kept in
~/.cache/novaos/wdf/umdf-<hash of the inputs> and copied from there while
nothing it is made from changed.
"""
import argparse, concurrent.futures, hashlib, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
KMDF = os.path.join(ROOT, 'tools', 'kmdf')
sys.path.insert(0, KMDF)
sys.path.insert(0, os.path.join(ROOT, 'tools', 'drivertest'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'authenticode'))
import build_kmdf as K                                          # noqa: E402
from mksys import import_libs, test_signer                      # noqa: E402
from sign import sign                                           # noqa: E402
from catalog import catalog                                     # noqa: E402

UMDF_HEADERS = '2.33'          # publicinc/wdf/umdf/<this>: the DDI list the source implements
OUTPUTS = ('Wudfx02000.dll', 'WUDFHost.exe', 'WUDFRd.sys', 'umdftest/umdftest.dll', 'umdftest/umdftest.inf',
           'umdftest/umdftest.cat')
# what the framework and the host import (all of them NovaOS's own DLLs)
FX_LIBS = ('kernel32', 'ntdll', 'advapi32', 'msvcrt', 'rpcrt4')
HOST_LIBS = ('kernel32', 'ntdll', 'advapi32', 'msvcrt')
CLIENT_LIBS = ('kernel32', 'msvcrt')
CACHE = K.CACHE


def run(cmd, **kw):
    return K.run(cmd, **kw)


def sources(fw):
    """The user-mode framework: every .c/.cpp under framework/umdf and
    framework/shared except the kernel-mode (km, kmdf) halves"""
    out = []
    for top in ('umdf', 'shared'):
        for d, _, files in os.walk(os.path.join(fw, top)):
            parts = os.path.relpath(d, fw).split(os.sep)
            if 'km' in parts or 'kmdf' in parts:
                continue
            out += [os.path.join(d, f) for f in files if f.endswith(('.c', '.cpp'))]
    return sorted(out)


def header_dirs(fw, pub):
    dirs = [pub]
    for top in ('umdf', 'shared'):
        for d, _, files in os.walk(os.path.join(fw, top)):
            parts = os.path.relpath(d, fw).split(os.sep)
            if 'km' in parts or 'kmdf' in parts:
                continue
            if any(f.endswith(('.h', '.hpp')) for f in files):
                dirs.append(d)
    return sorted(set(dirs), key=dirs.index)


def inputs_hash(userland):
    """A hash of everything the outputs are made from (with the compiler's
    version and the import libraries the outputs link)"""
    h = hashlib.sha256(K.WDF_COMMIT.encode())
    files = [os.path.join(d, n) for top in (HERE, os.path.join(KMDF, 'inc'), os.path.join(KMDF, 'patches'))
             for d, _, ns in os.walk(top) if '__pycache__' not in d for n in ns]
    files += [os.path.join(ROOT, *p.split('/')) for p in (
        'tools/kmdf/build_kmdf.py', 'tools/kmdf/rt/wpp.c', 'tools/kmdf/rt/bindhelper.cpp',
        'tools/drivertest/umdftest.c', 'tools/drivertest/umdftest.h', 'tools/drivertest/umdftest.inf',
        'tools/drivertest/mksys.py', 'kernel/nt/exports.h', 'kernel/nt/wdm.h', 'tools/authenticode/sign.py',
        'tools/authenticode/catalog.py')]
    files += [os.path.join(userland, n) for n in ('crt0.obj', 'tlssup.obj')]
    files += [os.path.join(userland, n + '.lib') for n in sorted(set(FX_LIBS + HOST_LIBS + CLIENT_LIBS))]
    for path in sorted(files):
        h.update(os.path.relpath(path, ROOT).encode() + b'\0' + open(path, 'rb').read())
    for tool in ('clang', 'lld-link'):
        h.update(subprocess.run([tool, '--version'], capture_output=True).stdout)
    return h.hexdigest()[:16]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('userland', help="build_userland.py's x64 output folder (crt0.obj, the import libraries)")
    ap.add_argument('--src', help='a patched checkout of Windows-Driver-Frameworks (default: fetch the pinned commit and apply tools/kmdf/patches)')
    ap.add_argument('--keep-going', action='store_true', help='compile every file and list each failure')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 2)
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    userland = os.path.abspath(args.userland)
    os.makedirs(os.path.join(out, 'umdftest'), exist_ok=True)
    kept = None if args.src else os.path.join(CACHE, 'umdf-' + inputs_hash(userland))
    if kept and all(os.path.isfile(os.path.join(kept, n)) for n in OUTPUTS):
        for n in OUTPUTS:
            shutil.copy(os.path.join(kept, n), os.path.join(out, n))
        print(f'build_umdf: {", ".join(os.path.basename(n) for n in OUTPUTS)} unchanged (from {kept})')
        return
    work = os.path.join(out, 'obj')
    os.makedirs(work, exist_ok=True)
    src = args.src or K.patched_tree(K.fetch(CACHE), os.path.join(work, 'src'))
    fw = os.path.join(src, 'src', 'framework')
    pub = os.path.join(src, 'src', 'publicinc', 'wdf', 'umdf', UMDF_HEADERS)
    inc, kinc = os.path.join(HERE, 'inc'), os.path.join(KMDF, 'inc')
    case = os.path.join(work, 'case')
    if os.path.isdir(case):
        shutil.rmtree(case)

    files = sources(fw)
    dirs = header_dirs(fw, pub)
    host = sorted(os.path.join(HERE, 'host', n) for n in os.listdir(os.path.join(HERE, 'host')) if n.endswith('.cpp'))
    client_src = [os.path.join(HERE, 'stub.c'), os.path.join(ROOT, 'tools', 'drivertest', 'umdftest.c')]
    allfiles = files + host + client_src + [os.path.join(d, f) for d in dirs for f in os.listdir(d)] + \
        [os.path.join(inc, f) for f in os.listdir(inc)]
    K.case_overlay(case, allfiles, dirs + [inc, kinc, K.MINGW])
    gen = os.path.join(work, 'gen')
    K.struct_list(pub, gen)

    resdir = subprocess.run(['clang', '-print-resource-dir'], capture_output=True, text=True).stdout.strip()
    common = ['--target=x86_64-pc-windows-msvc', '-O2', '-fno-stack-protector', '-fms-extensions',
              '-fgnuc-version=13.2', '-fno-strict-aliasing', '-nostdinc', '-w', '-Wno-c++11-narrowing', '-Wno-invalid-token-paste',
              '-ferror-limit=0', '-isystem', os.path.join(resdir, 'include'), '-I', inc, '-I', kinc, '-I', case] + \
             sum((['-I', d] for d in dirs), []) + \
             ['-I', gen, '-isystem', K.MINGW, '-idirafter', os.path.join(K.MINGW, 'ddk'),
              '-include', os.path.join(inc, 'novaos_umdf.h')]
    cxx = ['-std=c++17', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics', '-fno-sized-deallocation',
           '-fms-compatibility']

    def compile_one(job):
        path, obj, flags = job
        cmd = ['clang++' if path.endswith('.cpp') else 'clang'] + flags + (cxx if path.endswith('.cpp') else []) + \
              ['-c', path, '-o', obj]
        r = subprocess.run(cmd, capture_output=True, text=True)
        return path, obj, r.returncode, r.stderr

    def compile_all(paths, base, flags):
        jobs = [(p, os.path.join(work, os.path.relpath(p, base).replace(os.sep, '_') + '.o'), flags) for p in paths]
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
            sys.exit(f'build_umdf: {len(failed)} of {len(paths)} files failed')
        return objs

    fx = compile_all(files, fw, common)
    rt = compile_all(sorted(os.path.join(HERE, 'rt', n) for n in os.listdir(os.path.join(HERE, 'rt'))
                            if n.endswith(('.c', '.cpp'))), HERE, common)
    hostobjs = compile_all(host, HERE, common)
    # a client driver is built with the WDK's public headers only (and the
    # stub with fxldr.h, the loader interface the framework registers with)
    client = ['--target=x86_64-pc-windows-msvc', '-O2', '-fno-stack-protector', '-fms-extensions',
              '-fgnuc-version=13.2', '-nostdinc', '-Wall', '-isystem', os.path.join(resdir, 'include'), '-I', inc,
              '-isystem', pub, '-isystem', case, '-isystem', os.path.join(fw, 'shared', 'inc', 'private', 'common'),
              '-isystem', kinc, '-isystem', K.MINGW, '-include', os.path.join(inc, 'novaos_umdf_client.h')]
    clientobjs = compile_all(client_src, ROOT, client)
    # the reflector: a kernel-mode driver, built as tools/drivertest/mksys.py builds one
    rd = os.path.join(work, 'wudfrd.o')
    run(['clang', '--target=x86_64-pc-windows-msvc', '-O2', '-ffreestanding', '-fno-stack-protector',
         '-mno-stack-arg-probe', '-fno-builtin', '-fasync-exceptions', '-Wall', '-c', os.path.join(HERE, 'wudfrd.c'),
         '-o', rd])
    print(f'build_umdf: compiled {len(fx)} framework files')

    def libs(names):
        return [os.path.join(userland, n + '.lib') for n in names]
    common_link = ['/nodefaultlib', '/machine:x64', '/opt:ref', '/opt:icf', '/errorlimit:0', '/Brepro']
    run(['lld-link', '/dll', '/entry:DllMain', '/base:0x7ff7a0000000', '/export:FxFrameworkEntryUm'] + common_link + fx + rt + libs(FX_LIBS) +
        [f'/out:{os.path.join(out, "Wudfx02000.dll")}', f'/implib:{os.path.join(work, "Wudfx02000.lib")}',
         f'/map:{os.path.join(work, "Wudfx02000.map")}'])
    crt = [o for o in rt if os.path.basename(o) in ('rt_crt.cpp.o', 'rt_uuid.c.o')]
    run(['lld-link', '/subsystem:console', '/entry:mainCRTStartup'] + common_link +
        [os.path.join(userland, 'crt0.obj'), os.path.join(userland, 'tlssup.obj')] + hostobjs + crt +
        libs(HOST_LIBS) + [f'/out:{os.path.join(out, "WUDFHost.exe")}'])
    run(['lld-link', '/dll', '/entry:FxStubDllMain', '/base:0x7ff7b0000000'] + common_link + clientobjs +
        libs(CLIENT_LIBS) + [f'/out:{os.path.join(out, "umdftest", "umdftest.dll")}',
                             f'/implib:{os.path.join(work, "umdftest.lib")}'])
    run(['lld-link', '/driver', '/subsystem:native', '/entry:DriverEntry', '/nodefaultlib', '/machine:x64',
         '/base:0x140000000', '/Brepro', rd] + import_libs(work) + [f'/out:{os.path.join(out, "WUDFRd.sys")}'])

    # signed as NovaOS's other test drivers are: the reflector embedded (it
    # loads with test signing on, docs/drivers.md), the test driver's
    # package by a catalog of its INF and DLL
    signer, root = test_signer()
    path = os.path.join(out, 'WUDFRd.sys')
    data = open(path, 'rb').read()
    with open(path, 'wb') as f:
        f.write(sign(data, signer, [root], 'sha256'))
    dll = open(os.path.join(out, 'umdftest', 'umdftest.dll'), 'rb').read()
    inf = open(os.path.join(ROOT, 'tools', 'drivertest', 'umdftest.inf'), 'rb').read()
    with open(os.path.join(out, 'umdftest', 'umdftest.inf'), 'wb') as f:
        f.write(inf)
    with open(os.path.join(out, 'umdftest', 'umdftest.cat'), 'wb') as f:
        f.write(catalog([('umdftest.inf', inf), ('umdftest.dll', dll)], signer, [root]))
    print(f'build_umdf: linked Wudfx02000.dll, WUDFHost.exe, WUDFRd.sys and umdftest.dll in {out}')
    if kept:                                       # (the last build only)
        for old in os.listdir(CACHE):
            if old.startswith('umdf-'):
                shutil.rmtree(os.path.join(CACHE, old), ignore_errors=True)
        os.makedirs(os.path.join(kept, 'umdftest'))
        for n in OUTPUTS:
            shutil.copy(os.path.join(out, n), os.path.join(kept, n))


if __name__ == '__main__':
    main()
