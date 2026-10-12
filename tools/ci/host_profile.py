#!/usr/bin/env python3
"""Where the host's time goes during gltest fps.

    tools/ci/host_profile.py OUT_DIR &

gltest fps runs each of its turns (virgl direct, virgl through the GDI,
the GDI again, direct again, then llvmpipe) as a child "gltest.exe
fpsrun".  This follows the guest's serial log and, for each such child,
adds up the CPU time QEMU's threads used between the kernel's "Started"
and "exited" lines, grouped by thread name (QEMU's main thread, where
virglrenderer draws and copies; the vCPUs; llvmpipe's workers; ...).  One
line per turn goes to stdout and to OUT_DIR/host-profile.log: wall time,
then each group's CPU milliseconds per second of wall time.
"""
import glob, os, re, sys, time

TURNS = ['virgl direct', 'virgl GDI', 'virgl GDI', 'virgl direct', 'llvmpipe']
HZ = os.sysconf('SC_CLK_TCK')


def qemu_pid():
    for p in glob.glob('/proc/[0-9]*'):
        try:
            if open(p + '/comm').read().startswith('qemu-system'):
                return int(p[6:])
        except OSError:
            pass
    return None


def serial_path(pid):
    try:
        for a in open(f'/proc/{pid}/cmdline').read().split('\0'):
            if a.startswith('file:'):
                return a[5:]
    except OSError:
        pass
    return None


def snapshot(pid):
    """{thread group name: CPU ticks} of @pid's threads"""
    groups = {}
    for t in glob.glob(f'/proc/{pid}/task/[0-9]*'):
        try:
            head, rest = open(t + '/stat').read().rsplit(')', 1)
            f = rest.split()
            name = re.sub(r'(?<=[-/ ])[0-9]+', 'N', head.split('(', 1)[1])
            groups[name] = groups.get(name, 0) + int(f[11]) + int(f[12])
        except (OSError, IndexError, ValueError):
            pass
    return groups


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    pid = path = None
    while not path:
        pid = qemu_pid()
        path = pid and serial_path(pid)
        time.sleep(1)
    log = open(os.path.join(out, 'host-profile.log'), 'a')
    turn, start, pos, buf = 0, None, 0, ''
    while True:
        try:
            with open(path, errors='replace') as f:
                f.seek(pos)
                buf += f.read()
                pos = f.tell()
        except OSError:
            pass
        *lines, buf = buf.split('\n')
        for line in lines:
            if 'Started gltest.exe' in line:
                start = None
            elif 'gltest.exe' in line and 'fpsrun' in line and 'command line' in line:
                start = (time.monotonic(), snapshot(pid))
            elif start and re.search(r'gltest\.exe \(PID \d+\) exited', line):
                wall = time.monotonic() - start[0]
                now = snapshot(pid)
                use = {k: (v - start[1].get(k, 0)) * 1000 / HZ / wall for k, v in now.items()}
                parts = ', '.join(f'{k} {v:.0f}' for k, v in sorted(use.items(), key=lambda kv: -kv[1]) if v >= 5)
                name = TURNS[turn % len(TURNS)]
                msg = f'host profile: {name}: {wall:.1f} s; CPU ms a second: {parts}'
                print(msg, flush=True)
                log.write(msg + '\n')
                log.flush()
                turn += 1
                start = None
        if not os.path.exists(f'/proc/{pid}'):
            return
        time.sleep(0.2)


if __name__ == '__main__':
    main()
