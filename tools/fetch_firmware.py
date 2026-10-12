#!/usr/bin/env python3
"""Fetch NovaOS's default firmware set, as Linux distributions ship linux-firmware.

    tools/fetch_firmware.py [--set ID ...] [--from DIR] [--no-sof] [--list]

NovaOS's drivers are its own and built into the kernel (docs/drivers.md
lists them), but many devices run firmware their driver has to load into
them: a GPU's display and power microcontrollers, a Wi-Fi card, a
Bluetooth radio, an audio DSP.  Vendors publish those files through the
linux-firmware repository under licences that allow redistributing them
unchanged.  tools/firmware.json names the files NovaOS ships, grouped in
sets by device, pinned to one linux-firmware tag and each file's SHA-256.
This script downloads them (git.kernel.org, then the GitLab mirror, with
the same pauses as tools/download_cache.py), checks every hash and writes
them into third_party/firmware (ignored by git) in linux-firmware's own
layout, with their licence files; tools/build_userland.py builds that tree
into C:\\Windows\\Firmware, so i915/adlp_dmc.bin becomes
C:\\Windows\\Firmware\\i915\\adlp_dmc.bin.  Files already there with the
right hash are kept, so a second run downloads nothing.

It also runs tools/fetch_sof_firmware.py --all (the audio DSP's firmware,
which comes from the SOF project's own releases) unless --no-sof is given.

--set limits the download to some sets (repeatable; default all); --from
copies from a local linux-firmware checkout instead of the network; --list
prints the sets.  Without the firmware NovaOS still builds and runs;
release builds fetch it (.github/workflows/build.yml).
"""
import argparse, hashlib, json, os, shutil, subprocess, sys, tempfile, time, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, 'tools', 'firmware.json')
OUT = os.path.join(ROOT, 'third_party', 'firmware')
PAUSES = (10, 30, 60, 120)        # seconds between attempts, as tools/download_cache.py


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def load():
    with open(MANIFEST) as f:
        return json.load(f)


def wanted(m, ids):
    """The files of the chosen sets and the licence files they name."""
    sets = [s for s in m['sets'] if not ids or s['id'] in ids]
    unknown = set(ids or ()) - {s['id'] for s in m['sets']}
    if unknown:
        sys.exit(f'unknown firmware set: {", ".join(sorted(unknown))}')
    notes = {p for s in sets for p in (s['licence'], s.get('notice')) if p}
    files = [f for s in sets for f in s['files']]
    files += [f for f in m['licences'] if f['path'] in notes]
    return files


def download(src, path, dest):
    urls = [u.format(path=path, tag=src['tag']) for u in src['urls']]
    for attempt, pause in enumerate((0,) + PAUSES):
        if pause:
            print(f'  retrying in {pause} s')
            time.sleep(pause)
        for url in urls:
            try:
                req = urllib.request.Request(url, headers={'User-Agent': 'NovaOS-fetch-firmware'})
                with urllib.request.urlopen(req, timeout=120) as r, open(dest, 'wb') as f:
                    shutil.copyfileobj(r, f)
                return
            except OSError as e:
                print(f'  {url}: {e}')
    sys.exit(f'{path}: could not download it from linux-firmware {src["tag"]}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--set', action='append', dest='sets', metavar='ID')
    ap.add_argument('--from', dest='local', metavar='DIR')
    ap.add_argument('--no-sof', action='store_true')
    ap.add_argument('--list', action='store_true')
    args = ap.parse_args()
    m = load()
    if args.list:
        for s in m['sets']:
            size = sum(f['size'] for f in s['files'])
            print(f'{s["id"]:20} {size // 1024:6} KB  {s["device"]} (Linux: {s["linux_driver"]})')
        return

    src = m['source']
    got = 0
    for f in wanted(m, args.sets):
        dest = os.path.join(OUT, *f['path'].split('/'))
        if os.path.isfile(dest) and sha256(dest) == f['sha256']:
            continue
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        fd, tmp = tempfile.mkstemp(dir=os.path.dirname(dest), prefix='.fetch-')
        os.close(fd)
        try:
            if args.local:
                shutil.copyfile(os.path.join(args.local, *f['path'].split('/')), tmp)
            else:
                print(f'downloading {f["path"]}')
                download(src, f['path'], tmp)
            digest = sha256(tmp)
            if digest != f['sha256']:
                sys.exit(f'{f["path"]}: SHA-256 {digest}, expected {f["sha256"]} '
                         f'(linux-firmware {src["tag"]})')
            os.replace(tmp, dest)
        finally:
            if os.path.exists(tmp):
                os.unlink(tmp)
        got += 1
    with open(os.path.join(OUT, 'VERSION'), 'w') as f:
        f.write(f'{src["name"]} {src["tag"]}\n')
    print(f'{got} file(s) fetched into {OUT}')

    if not args.no_sof:
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'fetch_sof_firmware.py'), '--all'],
                       check=True)


if __name__ == '__main__':
    main()
