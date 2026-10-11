"""Stage Mesa's software OpenGL (llvmpipe) for the OS image.

NovaOS's own opengl32.dll hands every OpenGL call to Mesa's when it is
there (userland/opengl32/opengl32.c), so with these files on drive C: every
program gets OpenGL 4.5 on the CPU with nothing to install, as Windows
programs expect a display driver's OpenGL to be there.  The files are the
unmodified 64- and 32-bit WGL build of mesa-dist-win (MIT), the same
pinned, checksum-verified archive the App Store's "Mesa 3D" entry lists:
Mesa's opengl32.dll installed as opengl32_mesa.dll, with libgallium_wgl.dll
(llvmpipe) and libglapi.dll beside it.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import urllib.request

VERSION = '24.2.4'
URL = (f'https://github.com/pal1000/mesa-dist-win/releases/download/{VERSION}/'
       f'mesa3d-{VERSION}-release-msvc.7z')
SHA256 = 'e3919de795bee231edc88e9c996f2c1851250dff3bc9b51b9121760ae2250db0'
# archive path: installed name, for x64 (System32) and x86 (SysWOW64)
FILES = {'opengl32.dll': 'opengl32_mesa.dll', 'libgallium_wgl.dll': 'libgallium_wgl.dll',
         'libglapi.dll': 'libglapi.dll'}
SYSDIRS = {'x64': r'\Windows\System32', 'x86': r'\Windows\SysWOW64'}


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def download(url, target, digest):
    target = Path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.is_file() and sha256(target) == digest:
        return
    fd, temporary = tempfile.mkstemp(prefix=target.name + '.', dir=target.parent)
    os.close(fd)
    try:
        with urllib.request.urlopen(url, timeout=120) as src, open(temporary, 'wb') as dst:
            shutil.copyfileobj(src, dst)
        if sha256(temporary) != digest:
            raise ValueError('Mesa download is empty or its SHA-256 does not match')
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def payload_files(root):
    """(drive C: path, file) for every staged file; fails when one is missing"""
    result = []
    for arch, sysdir in SYSDIRS.items():
        for name, installed in FILES.items():
            path = Path(root) / arch / name
            if path.is_symlink() or not path.is_file():
                raise ValueError(f'Mesa payload lacks {arch}/{name}')
            result.append((sysdir + '\\' + installed, str(path)))
    return result


def stage(out):
    cache = Path(os.environ.get('NOVA_MESA_CACHE', Path.home() / '.cache/novaos/mesa')) / VERSION
    archive = cache / f'mesa3d-{VERSION}-release-msvc.7z'
    download(URL, archive, SHA256)
    extractor = shutil.which('7z') or shutil.which('7zz')
    if not extractor:
        raise RuntimeError('bundled Mesa requires 7z (install p7zip-full or 7zip)')
    destination = Path(out) / 'mesa'
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='mesa-', dir=destination.parent) as work:
        wanted = [f'{arch}/{name}' for arch in SYSDIRS for name in FILES]
        subprocess.run([extractor, 'x', '-y', f'-o{work}', str(archive)] + wanted,
                       check=True, stdout=subprocess.DEVNULL)
        payload_files(work)
        provenance = {'version': VERSION, 'source_url': URL, 'sha256': SHA256}
        (Path(work) / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        if destination.exists():
            shutil.rmtree(destination)
        shutil.move(work, destination)
    print(f'Bundled Mesa {VERSION} (llvmpipe): SHA-256 verified')
    return payload_files(destination)
