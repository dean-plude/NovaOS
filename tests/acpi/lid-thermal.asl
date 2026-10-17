/* A laptop's lid, thermal zone and processor throttling for the
 * self-tests (tools/selftest.py passes the compiled table to QEMU with
 * -acpitable).  The "embedded controller" is QEMU's pc-testdev: the test
 * sets the lid (port 0xE8: 0 open, 1 closed) and the temperature (port
 * 0xE9, in degrees C; 0 reads as 40 C) from the QEMU monitor, and raises
 * an event as a real embedded controller would: a GPE in a GPE block
 * device of its own (\_SB.GPE1, ACPI0006: status at port 0xEA, enable at
 * 0xEB), whose _E00 tells the lid to look again; its GPE 1 wakes the
 * machine when the lid opens.  (pc-testdev's ports
 * are plain storage, not write-one-to-clear, so _E00 clears the status
 * itself.)  The thermal zone, polled every second, cools the first
 * processor passively; its throttling registers (_PTC) are pc-testdev's
 * scratch memory at 0xFF000000.  The xHCI controller (at 00:05.0 in the
 * test boot) is described as a wake device. */
DefinitionBlock ("", "SSDT", 2, "NOVA", "LIDTHERM", 1)
{
    OperationRegion (TDEV, SystemIO, 0xE8, 4)
    Field (TDEV, ByteAcc, NoLock, Preserve)
    {
        LIDC, 8,
        TMPC, 8,
        GSTS, 8                                     /* GPE1's status byte (its enable byte follows) */
    }

    Scope (\_SB)
    {
        Device (LID0)
        {
            Name (_HID, EisaId ("PNP0C0D"))
            /* opening it wakes the machine: GPE 1 of GPE1 (a wake GPE stays
             * off while awake, so not the lid's runtime event, GPE 0) */
            Name (_PRW, Package () { Package () { \_SB.GPE1, 1 }, 3 })
            Method (_LID) { Return (LIDC == 0) }
        }

        Device (GPE1)                               /* the embedded controller's events */
        {
            Name (_HID, "ACPI0006")
            Name (_UID, 1)
            Name (_CRS, ResourceTemplate () { IO (Decode16, 0xEA, 0xEA, 1, 2) })
            Method (_E00)                           /* the lid moved */
            {
                GSTS = 0
                Notify (\_SB.LID0, 0x80)
            }
        }

    }

    External (\_SB.PCI0, DeviceObj)
    Scope (\_SB.PCI0)
    {
        Device (XHC0)
        {
            Name (_ADR, 0x00050000)
            Name (_PRW, Package () { 0x0D, 3 })     /* PME# from the USB controller */
        }
    }

    /* T-states for the first processor: 100, 75, 50 and 25 percent */
    External (\_SB.CPUS.C000, DeviceObj)
    Scope (\_SB.CPUS.C000)
    {
        Name (_PTC, Package ()
        {
            ResourceTemplate () { Register (SystemMemory, 8, 0, 0xFF000000) },
            ResourceTemplate () { Register (SystemMemory, 8, 0, 0xFF000000) }
        })
        Name (_TSS, Package ()
        {
            Package () { 100, 1000, 0, 0x00, 0x00 },
            Package () { 75, 750, 0, 0x16, 0x16 },
            Package () { 50, 500, 0, 0x14, 0x14 },
            Package () { 25, 250, 0, 0x12, 0x12 }
        })
        Method (_TPC) { Return (0) }
    }

    Scope (\_TZ)
    {
        ThermalZone (TZ00)
        {
            Name (_TZP, 10)                          /* poll every 1.0 s */
            Method (_TMP)
            {
                Local0 = TMPC
                If (Local0 == 0) { Local0 = 40 }
                Return (2732 + Local0 * 10)
            }
            Method (_PSV) { Return (3332) }          /* 60 C */
            Method (_HOT) { Return (3632) }          /* 90 C */
            Method (_CRT) { Return (3682) }          /* 95 C */
            Name (_PSL, Package () { \_SB.CPUS.C000 })
            Name (_TC1, 2)
            Name (_TC2, 5)
            Name (_TSP, 10)                          /* every 1.0 s */
        }
    }
}
