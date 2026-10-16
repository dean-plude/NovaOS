# powertest: the lid and a thermal zone of tests/acpi/lid-thermal.asl, with
# pc-testdev as their embedded controller raising its events through a GPE
# block device; the test closes the lid, heats and cools the zone when
# powertest asks (close_lid, set_temp).  Heating it past its passive trip
# point throttles CPU 0 (the zone's _PSL) through _PTC/_TSS.
DOC = ('`powertest` (closing the lid in `tests/acpi/lid-thermal.asl` sleeps, its GPE in a GPE block device and a USB '
       'key wake it, the thermal zone\'s readings, passive cooling throttling CPU 0 to its deepest T-state and back)')
TESTS = [
    Test('powertest', 'powertest', [r'powertest: \d+ passed, 0 failed', r'\[SHELL\] Lid closed: sleeping',
                                    r'\[SLEEP\] Woke up', r'\[ACPI\] Lid open',
                                    r'TZ00: 70\.0 C, at or above the passive trip point \(60\.0 C\): passive cooling on',
                                    r'\\_SB_\.CPUS\.C000: T3 \(25% performance\) for passive cooling(?! \(its)',
                                    r'TZ00: 45\.0 C, below the passive trip point \(60\.0 C\): passive cooling off',
                                    r'\\_SB_\.CPUS\.C000: T0 \(100% performance\), not throttled(?! \(its)',
                                    r'\[ACPI\] Woken by \\_SB_\.LID0 \(lid\)'],
         acts=[(r'powertest: close the lid', close_lid), (r'powertest: heat to 70 C', set_temp(70)),
               (r'powertest: cool to 45 C', set_temp(45))],
         boot_expect=[r'\[ACPI\] SCI on IRQ \d+ \(GSI \d+\)', r'\[ACPI\] PCI interrupt routing: \d+ entries',
                      r'\[ACPI\] Lid \\_SB_\.LID0', r'\[ACPI\] Thermal zone \\_TZ_\.TZ00: 40\.0 C',
                      r'\[ACPI\] GPE block \\_SB_\.GPE1: 8 events at I/O 0xea, on the SCI',
                      r'\[ACPI\] Processor \\_SB_\.CPUS\.C000 \(CPU 0\): 4 T-states \(100% 75% 50% 25%\), '
                      r'through _PTC, memory 0xff000000, limit T0',
                      r'\[ACPI\] Wake device \\_SB_\.LID0 \(lid\): GPE 0x1 of \\_SB_\.GPE1',
                      r'\[ACPI\] Wake device \\_SB_\.PCI0\.XHC0 \(USB controller\)',
                      r'\[KBDHID\] [^\n]*keyboard[^\n]*wakes the machine'],
         timeout=300),
]
