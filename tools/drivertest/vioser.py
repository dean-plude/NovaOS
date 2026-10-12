#!/usr/bin/env python3
"""Fetch virtio-win's signed VirtIO serial driver package (vioser) for the vioser self-test.

    tools/drivertest/vioser.py DIR

The devices suite's vioser boot (tests/selftest/devices/vioser) installs
the real VirtIO serial driver, vioser.sys (a KMDF driver), on QEMU's
virtio-serial port with pnputil, so it runs on Microsoft's framework
(Wdf01000.sys) and NovaOS's Plug and Play manager.  The package is the
Windows 11 x64 one from virtio-win 0.1.302's guest tools installer
(virtio-win-gt-x64.msi, from fedorapeople.org, where the virtio-win
project publishes its releases), as Microsoft signed it (Windows Hardware
Compatibility Publisher); it is not kept in the repository.  This
downloads the installer once into the cache (~/.cache/novaos, or
$NOVA_CACHE), checks its SHA-256, takes the three files out with 7z and
writes them into DIR as vioser.inf, vioser.sys and vioser.cat, checking
each one's SHA-256 too.
"""
import hashlib, os, shutil, subprocess, sys, tempfile, time, urllib.request

MSI_URL = ('https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/archive-virtio/'
           'virtio-win-0.1.302-1/virtio-win-gt-x64.msi')
MSI_SHA256 = '4f4388468a7ac5286bd1f1924ef02e7281a60a600390a7cfcfd55809efc0889f'
# the installer's names for the files -> (the package's names, SHA-256)
FILES = {
    'FILE_vioser_w11_amd64.inf': ('vioser.inf', 'a7bb3cc2482e0e4df281b506efb85ad4c76dd8d5a1522e480e9da354568006c0'),
    'FILE_vioser_w11_amd64.sys': ('vioser.sys', 'a58268785ae06015e70299d75ee1da1a8032c3cf6acbd1fb7bf8a3ff93edc221'),
    'FILE_vioser_w11_amd64.cat': ('vioser.cat', '5ef0e1163ec107405daca1c1703c84c886b114905d086c93cd4eddecd73a4e19'),
}
PAUSES = (10, 30, 60, 120)        # seconds between attempts, as tools/download_cache.py


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def installer():
    """The cached installer, downloaded the first time"""
    cache = os.environ.get('NOVA_CACHE') or os.path.join(os.path.expanduser('~'), '.cache', 'novaos')
    path = os.path.join(cache, 'virtio-win-0.1.302-gt-x64.msi')
    if os.path.isfile(path) and sha256(path) == MSI_SHA256:
        return path
    os.makedirs(cache, exist_ok=True)
    for pause in (0,) + PAUSES:
        if pause:
            print(f'  retrying in {pause} s', flush=True)
            time.sleep(pause)
        fd, tmp = tempfile.mkstemp(dir=cache, prefix='.fetch-')
        try:
            req = urllib.request.Request(MSI_URL, headers={'User-Agent': 'NovaOS-selftest'})
            with urllib.request.urlopen(req, timeout=120) as r, os.fdopen(fd, 'wb') as f:
                shutil.copyfileobj(r, f)
            if sha256(tmp) != MSI_SHA256:
                raise OSError('wrong SHA-256')
            os.replace(tmp, path)
            return path
        except OSError as e:
            print(f'  {MSI_URL}: {e}', flush=True)
        finally:
            if os.path.exists(tmp):
                os.unlink(tmp)
    raise RuntimeError('could not download virtio-win\'s guest tools installer (vioser)')


def fetch(dest):
    """Write vioser.inf, vioser.sys and vioser.cat into @dest"""
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
    print(f'vioser package written to {sys.argv[1]}')
