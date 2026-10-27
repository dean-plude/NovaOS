#!/usr/bin/env python3
"""The performance budget (roadmap step 26.4): boot NovaOS once and
measure three things, report them and fail on a large regression.

    tools/ci/perf.py [--img build/nova.img] [--out perf-out]
                     [--budget tools/ci/perf-budget.json] [--summary FILE]

  boot time      QEMU started to the kernel's main loop (the desktop is up)
  Firefox start  `start firefox.exe` to its first page on the screen: the
                 bundled Firefox opens a local page whose whole background
                 is one colour, with a fresh profile on drive C:, and
                 screenshots are taken twice a second until a fifth of the
                 screen shows that colour
  smpstress      `smpstress scaling 0` on four CPUs: files and the registry
                 must scale at least half as well as bare system calls
                 (tools/ci/check-smpstress.py, the nightly's rule)

A time over its budget (tools/ci/perf-budget.json, about twice what the
CI runners take) fails, as do smpstress failing and Firefox never drawing
its page.  Times are judged only under KVM: under TCG (software emulation)
they are recorded, not judged.  The numbers go to OUT/perf.json, the step
summary (--summary) and standard output.  Exit status 0: within budget."""
import argparse, importlib.util, json, os, re, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from novarun import Nova  # noqa: E402

_spec = importlib.util.spec_from_file_location('check_smpstress', os.path.join(ROOT, 'tools/ci/check-smpstress.py'))
check_smpstress = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(check_smpstress)

FIREFOX = r'C:\Programs\Mozilla Firefox\core\firefox.exe'
PAGE_RGB = (0x3C, 0xB3, 0x71)       # the page's background: no NovaOS desktop colour
TOLERANCE = 8                       # per channel
COVER = 0.2                         # of the screen showing the page's colour: the page is drawn
PAGE = ('<!doctype html><html><head><meta charset="utf-8"><title>NovaOS performance budget</title></head>'
        '<body style="margin:0;background:#%02x%02x%02x;height:100vh"></body></html>\n' % PAGE_RGB)
# A fresh profile that opens the page and nothing else: no welcome, terms-of-use
# or default-browser pages (Mozilla's documented preferences), no colour
# management to shift the page's colour
USER_JS = '\n'.join(f'user_pref("{k}", {json.dumps(v)});' for k, v in {
    'browser.aboutwelcome.enabled': False, 'browser.startup.homepage_override.mstone': 'ignore',
    'startup.homepage_welcome_url': '', 'startup.homepage_welcome_url.additional': '',
    'browser.shell.checkDefaultBrowser': False, 'datareporting.policy.dataSubmissionEnabled': False,
    'datareporting.policy.firstRunURL': '', 'toolkit.telemetry.reportingpolicy.firstRun': False,
    'termsofuse.bypassNotification': True, 'app.update.auto': False, 'browser.startup.page': 0,
    'gfx.color_management.mode': 0}.items()) + '\n'


def ppm_cover(path, rgb=PAGE_RGB, step=8):
    """The share of the screenshot @path (a binary PPM) within TOLERANCE of
    @rgb, sampled every @step pixels across and down"""
    data = open(path, 'rb').read()
    m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', data)
    if not m:
        return 0.0
    w, h, start = int(m.group(1)), int(m.group(2)), m.end()
    hit = total = 0
    for y in range(0, h, step):
        row = start + y * w * 3
        for x in range(0, w, step):
            i = row + x * 3
            total += 1
            hit += all(abs(data[i + c] - rgb[c]) <= TOLERANCE for c in range(3))
    return hit / total if total else 0.0


def firefox_start(nova, out, limit):
    """Seconds from Enter on `start firefox.exe` to its page covering COVER
    of the screen; (None, why) if it never did"""
    ppm = os.path.join(out, 'screen.ppm')

    def cover():
        nova.qmp.cmd('screendump', filename=os.path.abspath(ppm))
        return ppm_cover(ppm)
    before = cover()
    if before >= COVER:
        return None, f'the page colour already covers {before:.0%} of the screen before Firefox starts'
    nova.sr.read_new()
    nova.qmp.type(f'start "{FIREFOX}" -no-remote -profile C:\\Perf\\profile C:\\Perf\\perf.html')
    t0 = time.time()
    nova.qmp.key('ret')
    log = ''
    while time.time() - t0 < limit:
        time.sleep(0.5)
        log += nova.sr.read_new()
        if cover() >= COVER:
            secs = time.time() - t0
            nova.shot(os.path.join(out, 'firefox.png'))
            return secs, None
        m = re.search(r'\[UM\] firefox\.exe \(PID \d+\) crashed[^\n]*', log, re.I)
        if m:
            nova.shot(os.path.join(out, 'firefox.png'))
            return None, m.group(0)
    nova.shot(os.path.join(out, 'firefox.png'))
    return None, f'its page was not on the screen after {limit} s'


def verdict(name, value, budget, judged):
    """(row text, over budget) for a time"""
    if value is None:
        return 'not measured', True
    if not judged:
        return 'recorded (TCG: not judged)', False
    return ('within budget', False) if value <= budget else (f'over budget by {value - budget:.1f} s', True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--img', default=os.path.join(ROOT, 'build', 'nova.img'))
    ap.add_argument('--out', default='perf-out')
    ap.add_argument('--budget', default=os.path.join(ROOT, 'tools/ci/perf-budget.json'))
    ap.add_argument('--summary')
    a = ap.parse_args()
    budget = json.load(open(a.budget))
    accel = os.environ.get('NOVARUN_ACCEL', 'kvm')
    judged = accel == 'kvm'
    os.makedirs(a.out, exist_ok=True)
    put = tempfile.mkdtemp(prefix='perf-page')
    os.makedirs(os.path.join(put, 'profile'))
    open(os.path.join(put, 'perf.html'), 'w').write(PAGE)
    open(os.path.join(put, 'profile', 'user.js'), 'w').write(USER_JS)

    res = {'accel': accel, 'boot_seconds': None, 'firefox_seconds': None, 'smpstress': {}}
    bad, notes = [], []
    nova = None
    try:
        nova = Nova(a.img, os.path.join(a.out, 'vm'), puts=[(put, 'C:\\Perf')], mem=4096, smp=4,
                    boot_timeout=300 if judged else 900, serial_path=os.path.join(a.out, 'serial.log'))
        res['boot_seconds'] = round(nova.booted - nova.launched, 1)
        print(f'boot: {res["boot_seconds"]} s', flush=True)

        log, ok = nova.run('smpstress scaling 0', 600 if judged else 900)
        if not ok:
            log += '\nTIMEOUT'
        open(os.path.join(a.out, 'smpstress.log'), 'w').write(log)
        smp_ok, why, scaling = check_smpstress.judge(log)
        res['smpstress'] = scaling
        notes += why
        print('smpstress: ' + '; '.join(why), flush=True)
        if not smp_ok:                  # (the artifact is not always at hand: the samples go in the job log too)
            print(log[-3000:], flush=True)
        if not smp_ok:
            bad.append('smpstress')

        secs, why = firefox_start(nova, a.out, 180 if judged else 900)
        res['firefox_seconds'] = round(secs, 1) if secs is not None else None
        if why:
            notes.append('Firefox: ' + why)
        print(f'Firefox start: {res["firefox_seconds"]} s{" (" + why + ")" if why else ""}', flush=True)
    except RuntimeError as e:
        notes.append(str(e).splitlines()[0])
        print(e, flush=True)
    finally:
        if nova:
            nova.close()

    rows = []
    for key, label in (('boot_seconds', 'Boot to the desktop'), ('firefox_seconds', 'Firefox start to its page')):
        text, over = verdict(label, res[key], budget[key], judged)
        if over:
            bad.append(label)
        value = '-' if res[key] is None else f'{res[key]:.1f} s'
        rows.append(f'| {label} | {value} | {budget[key]} s | {"❌ " if over else "✅ "}{text} |')
    x = res['smpstress']
    scal = ', '.join(f'{k.strip("()")} {v:.2f}x' for k, v in x.items() if k in ('(system calls)', 'files', 'registry'))
    rows.append(f'| smpstress scaling (4 CPUs) | {scal or "-"} | files and registry ≥ {check_smpstress.RATIO} '
                f'× system calls | {"❌ failed" if "smpstress" in bad else "✅ ok"} |')
    report = '\n'.join([f'### Performance budget ({accel})', '',
                        '| Measure | This run | Budget | Result |', '| --- | --- | --- | --- |', *rows, '',
                        *[f'- {n}' for n in notes], ''])
    print(report)
    json.dump({**res, 'budget': budget, 'failed': bad}, open(os.path.join(a.out, 'perf.json'), 'w'), indent=2)
    if a.summary:
        with open(a.summary, 'a') as f:
            f.write(report + '\n')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
