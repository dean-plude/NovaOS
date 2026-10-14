#!/usr/bin/env python3
"""Check a `smpstress scaling 0` log (tools/novarun.py output) from the
nightly run: every test passed, nothing hung, and, where the machine runs
the CPUs side by side, files and the registry scale at least half as well
as bare system calls do in the same run.

The bare system call loop (NtQuerySystemTime) takes no lock, so its scaling
is what the machine allows; work still under one big lock would stay near
1x while it reaches 3-4x.  A shared runner often does not run the emulated
CPUs side by side at all (system calls 0.6-1x, moving a lot from run to
run): there the figures are only recorded.  Exit status 0: pass."""
import re, sys

RATIO = 0.5           # of the system calls' scaling
PARALLEL = 2.0        # system calls scaling at least this: the CPUs ran side by side


def judge(log):
    """(passed, [lines saying why], {test: scaling}) for a smpstress log"""
    if 'TIMEOUT' in log:
        return False, ['smpstress hung (TIMEOUT)'], {}
    m = re.search(r'smpstress: (\d+) passed, (\d+) failed', log)
    if not m or m.group(2) != '0':
        return False, ['smpstress: ' + (m.group(0) if m else 'no result line')], {}
    x = {name.strip(): float(v) for name, v in
         re.findall(r'^  ([a-z() ]+?)\s+1 thread .*: ([0-9.]+)x$', log, re.M)}
    calls = x.get('(system calls)')
    if not calls:
        return False, ['no system call yardstick in the log'], x
    cpus = re.search(r'smpstress: (\d+) CPUs', log)
    if cpus:                            # (a slow one-thread sample can make it look better than possible)
        calls = min(calls, float(cpus.group(1)))
    if calls < PARALLEL:
        return True, [f'bare system calls scaled {calls}x: the CPUs did not run side by side here, '
                      'so scaling is recorded, not judged'], x
    ok, why = True, []
    for name in ('files', 'registry'):
        got = x.get(name)
        good = got is not None and got >= RATIO * calls
        why.append(f'{name}: {got}x against bare system calls {calls}x: {"ok" if good else "too low"}')
        ok = ok and good
    return ok, why, x


def main(path):
    ok, why, _ = judge(open(path, errors='replace').read())
    print('\n'.join(why))
    return 0 if ok else 1

if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
