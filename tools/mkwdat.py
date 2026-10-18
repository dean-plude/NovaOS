#!/usr/bin/env python3
"""Write an ACPI WDAT table (Microsoft's Hardware Watchdog Timers Design
Specification) describing the Intel ICH9 family's TCO watchdog, as firmware
on such boards describes it, for QEMU's q35 machine (-acpitable file=...).
NovaOS's WDAT driver (kernel/hal/wdat.c) runs it: the self-tests' recovery
boot (tests/selftest/devices/rollbacktest) checks that it resets the PC.

    mkwdat.py OUT [--pmbase 0x600]

PMBASE is the chipset's ACPI I/O base, 0x600 under OVMF on q35.  The TCO
timer resets the PC at its second timeout, so one count of the table is
two of the timer's 0.6 s ticks.  Turning the reset on takes the root
complex registers (RCBA, which OVMF leaves off: switched on in PCI
configuration space, the GAS's space 2) and clearing NO_REBOOT there.
"""
import argparse
import struct

# Actions
RESET, SET_COUNTDOWN, QUERY_RUNNING, SET_RUNNING, SET_STOPPED, SET_REBOOT, SET_STATUS = 0x01, 0x06, 0x08, 0x09, 0x0B, 0x11, 0x21
# Instructions
READ_VALUE, WRITE_VALUE, WRITE_COUNTDOWN, PRESERVE = 0, 2, 3, 0x80
MEM, IO, PCI = 0, 1, 2
RCBA = 0xFED1C000


def gas(space, bits, offset, addr):
    return struct.pack('<BBBBQ', space, bits, offset, {8: 1, 16: 2, 32: 3}[bits], addr)


def entry(action, ins, space, bits, addr, value, mask, offset=0):
    return struct.pack('<BBH', action, ins, 0) + gas(space, bits, offset, addr) + struct.pack('<II', value, mask)


def wdat(pmbase):
    tco = pmbase + 0x60
    lpc = (31 << 32) | (0 << 16)                      # bus 0, device 31, function 0
    e = [
        entry(SET_REBOOT, WRITE_VALUE, PCI, 32, lpc | 0xF0, RCBA | 1, 0xFFFFFFFF),          # RCBA on
        entry(SET_REBOOT, WRITE_VALUE | PRESERVE, MEM, 32, RCBA + 0x3410, 0, 1, offset=5),  # GCS NO_REBOOT off
        entry(SET_REBOOT, WRITE_VALUE | PRESERVE, IO, 32, pmbase + 0x30, 0, 1, offset=13),  # SMI_EN TCO_EN off
        entry(SET_COUNTDOWN, WRITE_COUNTDOWN | PRESERVE, IO, 16, tco + 0x12, 0, 0x3FF),    # TCO_TMR
        entry(RESET, WRITE_VALUE, IO, 16, tco + 0x00, 1, 0xFFFF),                           # TCO_RLD
        entry(RESET, WRITE_VALUE, IO, 16, tco + 0x04, 1, 1, offset=3),                      # TCO1_STS timeout (W1C)
        entry(SET_STATUS, WRITE_VALUE, IO, 16, tco + 0x06, 3, 3, offset=1),                 # TCO2_STS (W1C)
        entry(QUERY_RUNNING, READ_VALUE, IO, 16, tco + 0x08, 0, 1, offset=11),              # TCO_TMR_HLT clear
        entry(SET_RUNNING, WRITE_VALUE | PRESERVE, IO, 16, tco + 0x08, 0, 1, offset=11),
        entry(SET_STOPPED, WRITE_VALUE | PRESERVE, IO, 16, tco + 0x08, 1, 1, offset=11),
    ]
    body = struct.pack('<IHBBB3sIIIB3sI', 32, 0, 0xFF, 0xFF, 0xFF, b'\0' * 3,
                       1200, 1023, 4, 0x01, b'\0' * 3, len(e)) + b''.join(e)
    length = 36 + len(body)
    head = struct.pack('<4sIBB6s8sI4sI', b'WDAT', length, 1, 0, b'NOVAOS', b'NOVAWDAT', 1, b'NOVA', 1)
    table = bytearray(head + body)
    table[9] = (-sum(table)) & 0xFF
    return bytes(table)


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('out')
    p.add_argument('--pmbase', type=lambda v: int(v, 0), default=0x600)
    a = p.parse_args()
    with open(a.out, 'wb') as f:
        f.write(wdat(a.pmbase))


if __name__ == '__main__':
    main()
