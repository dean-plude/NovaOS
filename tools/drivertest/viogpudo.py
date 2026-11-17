#!/usr/bin/env python3
"""Fetch virtio-win's signed VirtIO GPU display-only driver package (viogpudo) for its self-test.

    tools/drivertest/viogpudo.py DIR

The devices suite's viogpudo boot (tests/selftest/devices/viogpudo)
installs Red Hat's WDDM display-only driver for QEMU's virtio GPU,
viogpudo.sys, unchanged on the machine's virtio-vga with pnputil: it
registers with NovaOS's dxgkrnl (kernel/nt/dxgkrnl.c) and the desktop moves
to it.  The package is the Windows 11 x64 one from the same virtio-win
0.1.302 guest tools installer as vioser's (tools/drivertest/vioser.py
downloads and caches it), as Microsoft signed it (Windows Hardware
Compatibility Publisher); it is not kept in the repository.  This takes
the three files out with 7z and writes them into DIR as viogpudo.inf,
viogpudo.sys and viogpudo.cat, checking each one's SHA-256.
"""
import hashlib, os, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vioser import installer, sha256  # noqa: E402  (the same installer, downloaded once)

# the installer's names for the files -> (the package's names, SHA-256)
FILES = {
    'FILE_viogpudo_w11_amd64.inf': ('viogpudo.inf', 'be012d9ed48645bb966483bf15e70cdf5eb0f84bd3bbeb1f4d4aec2d096d5c0d'),
    'FILE_viogpudo_w11_amd64.sys': ('viogpudo.sys', 'adb624a34dc1b6519af0601268af7c1e3e8972c16c69467f6e3144e66b67f5e1'),
    'FILE_viogpudo_w11_amd64.cat': ('viogpudo.cat', 'f6803e01dedd4168a8fa0e4caa11fa93b4150735c4d32f4a9369c9758e57ae20'),
}


def fetch(dest):
    """Write viogpudo.inf, viogpudo.sys and viogpudo.cat into @dest"""
    msi = installer()
    os.makedirs(dest, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(['7z', 'e', '-y', f'-o{tmp}', msi] + list(FILES), check=True, stdout=subprocess.DEVNULL)
        for src, (name, digest) in FILES.items():
            path = os.path.join(tmp, src)
            if not os.path.isfile(path) or sha256(path) != digest:
                raise RuntimeError(f'{src} in the installer is missing or not the expected file')
            shutil.copy(path, os.path.join(dest, name))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__.split('\n\n')[1])
    fetch(sys.argv[1])
    print(f'viogpudo package written to {sys.argv[1]}')
