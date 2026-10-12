#!/usr/bin/env python3
"""mksys.py — builds wdmtest.sys, the Windows kernel-mode test driver

    mksys.py OUTDIR

tools/build_userland.py runs it and puts what it writes in C:\\Tests\\Wdm.
The driver (tools/drivertest/wdmtest.c) is built as a Windows driver is:
clang for x86_64-pc-windows-msvc, linked by lld-link /driver against an
import library for ntoskrnl.exe and hal.dll.  The import libraries are made
here from the functions NovaOS's kernel exports to drivers
(kernel/nt/exports.h), so the WDK is not needed.
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(' '.join(cmd) + '\n' + r.stdout + r.stderr)
        sys.exit(1)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    work = os.path.join(out, 'obj')
    os.makedirs(work, exist_ok=True)
    mods = {}
    for m in re.finditer(r'^\s*NT_EXPORT\(\s*([\w.]+)\s*,\s*(\w+)\s*\)', open(os.path.join(ROOT, 'kernel', 'nt', 'exports.h')).read(), re.M):
        mods.setdefault(m.group(1), []).append(m.group(2))
    libs = []
    for mod, names in sorted(mods.items()):
        stem = os.path.splitext(mod)[0]
        d = os.path.join(work, stem + '.def')
        with open(d, 'w') as f:
            f.write(f'LIBRARY {mod}\nEXPORTS\n' + ''.join(f'    {n}\n' for n in names))
        lib = os.path.join(work, stem + '.lib')
        run(['llvm-lib', f'/def:{d}', '/machine:x64', f'/out:{lib}'])
        libs.append(lib)
    obj = os.path.join(work, 'wdmtest.obj')
    run(['clang', '--target=x86_64-pc-windows-msvc', '-O2', '-ffreestanding', '-fno-stack-protector',
         '-mno-stack-arg-probe', '-fno-builtin', '-Wall', '-c', os.path.join(HERE, 'wdmtest.c'), '-o', obj])
    run(['lld-link', '/driver', '/subsystem:native', '/entry:DriverEntry', '/nodefaultlib', '/machine:x64',
         '/base:0x140000000', obj] + libs + [f'/out:{os.path.join(out, "wdmtest.sys")}'])
    for n in os.listdir(work):
        os.remove(os.path.join(work, n))
    os.rmdir(work)


if __name__ == '__main__':
    main()
