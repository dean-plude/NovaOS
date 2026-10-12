#!/usr/bin/env python3
"""Cross-build VKD3D-Proton (Direct3D 12 on Vulkan) for the App Store.

    tools/build_vkd3d_proton.py OUT_DIR [--work DIR]

The App Store's "VKD3D-Proton" entry installs OUT_DIR/vkd3d-proton.7z:
vkd3d-proton-VERSION\\x64 and \\x86, each with d3d12.dll (NovaOS installs it
as d3d12_vkd3d.dll behind its own d3d12.dll) and d3d12core.dll, plus the
licence.  The CI publishes it beside nova.iso on the "latest" release.

It is the VKD3D-Proton release named below, unmodified, built with a newer
commit of its DXIL-to-SPIR-V translator (dxil-spirv, MIT, by the same
author) than the release pins: 3.0.1's translator turns the branches of two
of Godot 4.7's Forward+ compute shaders (SDFGI) into SPIR-V whose selection
constructs are not properly nested (spirv-val: "exits the selection ... but
not via a structured exit"), which Mesa's SPIR-V reader refuses, so the
pipelines fail with VK_ERROR_UNKNOWN on every Vulkan driver built on Mesa.
dxil-spirv's "Add yet another tie-breaking case for selection merges"
(2a480c1, 2026-08-20) fixes it upstream; no VKD3D-Proton release carries it
yet.  When one does, point VERSION at it, drop DXIL_SPIRV and the store can
go back to the official download.

VKD3D-Proton is LGPL-2.1: this script, the pinned upstream commits and the
licence in the archive are its corresponding source.

Needs git, meson (1.4 or newer), ninja, glslangValidator, MinGW-w64 gcc and
g++ for x86-64 and i686 with widl (mingw-w64-tools) and 7z.  Later runs
reuse the work directory (default OUT_DIR/work).
"""
import argparse
import os
import shutil
import subprocess
import sys

VERSION = '3.0.1'
COMMIT = '3b10bd7a7ec6a7347e616cf8bea59333afec2255'        # tag v3.0.1
REPO = 'https://github.com/HansKristian-Work/vkd3d-proton.git'
DXIL_SPIRV = '2a480c18fb99d9e649ed41e1f06ee1435a168905'    # the selection-merge fix


def git(*args, cwd=None):
    subprocess.run(['git', *args], cwd=cwd, check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('out')
    ap.add_argument('--work')
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    work = os.path.abspath(a.work or os.path.join(out, 'work'))
    src = os.path.join(work, 'vkd3d-proton')
    os.makedirs(out, exist_ok=True)
    if not os.path.isdir(os.path.join(src, '.git')):
        os.makedirs(work, exist_ok=True)
        git('clone', '-q', '--branch', f'v{VERSION}', REPO, src)
    git('checkout', '-q', COMMIT, cwd=src)
    git('submodule', 'update', '--init', '--recursive', '-q', cwd=src)
    dxil = os.path.join(src, 'subprojects', 'dxil-spirv')
    if subprocess.run(['git', 'cat-file', '-e', DXIL_SPIRV + '^{commit}'], cwd=dxil).returncode:
        git('fetch', '-q', 'origin', DXIL_SPIRV, cwd=dxil)
    git('checkout', '-q', DXIL_SPIRV, cwd=dxil)
    git('submodule', 'update', '--init', '--recursive', '-q', cwd=dxil)

    dest = os.path.join(work, 'dist')
    shutil.rmtree(dest, ignore_errors=True)
    subprocess.run([os.path.join(src, 'package-release.sh'), VERSION, dest, '--no-package'], cwd=src, check=True)
    pkg = os.path.join(dest, f'vkd3d-proton-{VERSION}')
    for arch in ('x64', 'x86'):
        for dll in ('d3d12.dll', 'd3d12core.dll'):
            if not os.path.isfile(os.path.join(pkg, arch, dll)):
                sys.exit(f'build_vkd3d_proton: {arch}\\{dll} was not built')
    os.unlink(os.path.join(pkg, 'setup_vkd3d_proton.sh'))
    shutil.copy(os.path.join(src, 'LICENSE'), os.path.join(pkg, 'LICENSE'))
    with open(os.path.join(pkg, 'README.txt'), 'w', newline='\r\n') as f:
        f.write(f'VKD3D-Proton {VERSION} ({REPO}, commit {COMMIT}),\n'
                f'built with dxil-spirv {DXIL_SPIRV} by NovaOS tools/build_vkd3d_proton.py.\n'
                'LGPL-2.1 (LICENSE); the source is the upstream commits above.\n')
    archive = os.path.join(out, 'vkd3d-proton.7z')
    if os.path.exists(archive):
        os.unlink(archive)
    subprocess.run(['7z', 'a', '-mx=9', archive, f'vkd3d-proton-{VERSION}'], cwd=dest, check=True,
                   stdout=subprocess.DEVNULL)
    print(archive)


if __name__ == '__main__':
    main()
