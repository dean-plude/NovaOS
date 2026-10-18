# A laptop like the ThinkPad T14 Gen 4 (docs/hardware.md): no \_S3 (Modern
# Standby firmware), no HPET, an embedded controller behind the lid,
# battery and AC adapter, an LPS0 device, processor C-states in _CST
# (tests/acpi/laptop.asl) on a CPU with MWAIT, no
# display adapter NovaOS drives (the firmware's GOP only), nova.iso on a
# USB stick and an empty NVMe disk.  The idle CPUs wait in their deepest
# C-state, and CPU 1 stops its tick while idle (idletest).  The lid sleeps it in low-power S0
# idle and wakes it; then NovaOS installs itself on the NVMe disk and
# starts from it, through the firmware boot entry Setup added.
import time

DOC = ('a laptop without S3 whose lid and battery sit behind an embedded controller (`tests/acpi/laptop.asl`): '
       'the battery read through it, idle CPUs in the C-states its `_CST` describes (MWAIT and a port) with CPU 1\'s '
       'tick stopped while idle (`idletest`), the lid sleeping it in low-power S0 idle and waking it, then `install` '
       'onto an NVMe disk from the USB stick and the first start from that disk')


def _serial(nova):
    return open(nova.serial_path, 'rb').read().decode('latin-1')


def lid_sleep(nova):
    """Close the lid (pc-testdev port 0xE8, see tests/acpi/laptop.asl), wait
    until NovaOS sleeps in S0 idle, then open it and wait until it is awake"""
    nova.hmp('o /b 0xe8 1')
    for want in ('sleeping in low-power S0 idle', None, 'Woke up after'):
        if want is None:
            time.sleep(3)
            nova.hmp('o /b 0xe8 0')
            continue
        end = time.time() + 60
        while want not in _serial(nova) and time.time() < end:
            time.sleep(0.25)
    time.sleep(2)


TESTS = [
    Test('ec battery', 'battery', [r'Power source: battery', r'Battery: 60%', r'Time left: 4 h 00 min'],
         boot_expect=[r'S3 not supported', r'\[APIC\] Timer: .*(calibrated against the PIT|from CPUID 0x15) \(no HPET\)',
                      r'\[EC\] \\_SB_\.EC0_: the self-tests\' embedded controller \(a model, no I/O ports\), GPE 0xf',
                      r'\[ACPI\] Low-power S0 idle device \\_SB_\.PEPD: Intel functions 0x79',
                      r'\[ACPI\] Lid \\_SB_\.LID0', r'\[ACPI\] Battery \\_SB_\.BAT0', r'\[ACPI\] AC adapter \\_SB_\.AC0_']),
    Test('idle states', 'idletest 2', [r'idletest: \d+ passed, 0 failed'],
         boot_expect=[r'\[IDLE\] MWAIT yes[^\n]*, in a virtual machine',
                      r'\[IDLE\] CPU 0: C1 \(MWAIT 0x00, 1 us\), C2 \(I/O 0xe9, 50 us\), C3 \(MWAIT 0x20, 200 us\), '
                      r'from \\_SB_\.CPUS\.C000\._CST',
                      r'\[IDLE\] CPU 1: C1 \(MWAIT 0x00, 1 us\), C2 \(I/O 0xe9, 50 us\), C3 \(MWAIT 0x20, 200 us\), '
                      r'from \\_SB_\.CPUS\.C001\._CST']),
    Test('lid sleep (S0 idle)', 'cmd /c echo awake', [r'awake'], before=lid_sleep,
         boot_expect=[r'\[EC\] Event 0x2a: _Q2A', r'\[ACPI\] Lid closed', r'\[SHELL\] Lid closed: sleeping',
                      r'\[SLEEP\] No S3 on this machine: sleeping in low-power S0 idle',
                      r'\[ACPI\] LPS0: told the platform the screen is off',
                      r'\[SLEEP\] Woke up after \d+ s \(the lid opened\)', r'\[ACPI\] LPS0: left S0 idle']),
    Test('install on nvme', 'install nvme0n1', [r'NovaOS is installed'], builtin=True, timeout=900,
         boot_expect=[r'\[SETUP\] Installing NovaOS on nvme0n1',
                      r'\[SETUP\] Added the firmware boot entry Boot[0-9A-F]{4} "NovaOS" for nvme0n1, first in BootOrder']),
    Test('start from nvme', 'shutdown /r', [r'\[SETUP\] The firmware has a "NovaOS" boot entry for this disk'],
         reboot=True),
]
