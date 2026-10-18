# Installing and power on a laptop

The [reference machine](hardware.md), a Lenovo ThinkPad T14 Gen 4
(Intel), differs from QEMU in four ways that matter for installing
NovaOS and for its power handling:

| On the T14 | What NovaOS does |
|---|---|
| The disk is NVMe, possibly behind Intel VMD ("RST") | The NVMe driver installs to it and starts from it. VMD hides the disk; NovaOS names the VMD controller in its log and in `install`, and turning VMD off in the firmware setup shows the disk |
| The firmware starts what is in its boot entries | The installer writes `\EFI\BOOT\BOOTX64.EFI`, which firmware starts from a disk it has no entry for; the first start from the disk adds a "NovaOS" boot entry and puts it first |
| The lid, battery and charger sit behind the embedded controller | The ACPI embedded controller driver (`kernel/hal/ec.c`) gives uACPI the controller's registers and runs its events |
| No S3 sleep (Modern Standby firmware), and maybe no HPET | Sleep is low-power S0 idle with the LPS0 device's calls; the timers come from CPUID leaf 0x15 when there is no HPET |

**Status:** all of this is tested in QEMU (the devices suite's `laptop`
boot, [below](#the-qemu-test)). None of it has run on the T14 or any
other real laptop yet; the [checks on the T14](#checks-on-the-t14) at the
end are waiting to be done by hand (roadmap step 23.1).

## Installing

The Setup app (Install NovaOS on the live desktop) and the Terminal's
`install` command do the same thing. `install` alone lists the disks:

```
C:\Documents> install
  nvme0n1  488386 MiB  SAMSUNG MZVL2512HCJQ-00BL7  Volumes: SYSTEM, Windows
  usb0  14800 MiB  SanDisk Ultra  Volumes: NOVA_EFI  (NovaOS is running from it)
Usage: install <disk> [/beside | /shrink | /reinstall] [/fat]   (drive C: on NTFS, or FAT32 with /fat;
       /beside keeps what is on the disk and uses its free space, /shrink first shrinks Windows'
       volume to make that space, /reinstall replaces NovaOS keeping drive C:; else the disk is erased)
```

`install nvme0n1` erases that disk, makes a GPT with an EFI System
Partition and a partition for drive C: (NTFS, or FAT32 with `/fat`), and
copies the boot loader and the kernel from the installation medium. It
refuses the disk NovaOS is running from and a disk that is too small.

**Beside Windows.** `install` also lists each GPT disk's partitions and
its free space. `install nvme0n1 /beside` (or "Beside Windows" on Setup's
"Where should NovaOS go?" page) keeps every partition on the disk and puts
NovaOS's own EFI System Partition and drive C:'s partition in the largest
free space, which must hold NovaOS's ESP and 512 MiB for drive C:
(about 1.1 GB today; the Where page says how much). Windows' ESP, its reserved and
recovery partitions and its NTFS volume are not written to.

**Making room.** On a disk Windows fills (as on most PCs sold with it),
`install` says how far Windows' volume can shrink, and `install nvme0n1
/shrink` (or "Beside Windows, shrinking it from ... to ..." on the Where
page) shrinks it first: NovaOS gets half of Windows' free space (at least
what it needs, at most 64 GiB), files past the volume's new end are moved
below it, then the partition is made smaller and NovaOS goes in the room
after it. Windows must have been shut down fully: a hibernated Windows,
Fast Startup or a pending chkdsk leaves the volume as it is (turn Fast
Startup off in Windows' power options, or hold Shift while choosing Shut
down). A BitLocker-encrypted volume can't be shrunk from NovaOS: shrink it
in Windows' Disk Management. Back up first; see
[design/installer.md](design/installer.md) for how it is done.

**Reinstalling.** On a disk NovaOS is on, `install nvme0n1 /reinstall`
(or "Reinstall NovaOS, keeping drive C: and your files") formats only
NovaOS's EFI System Partition again with this system's files and keeps
drive C:'s partition, `NOVADATA`, as it is: `C:\Users`, your files,
settings and programs stay, and the firmware's "NovaOS" entry is reused.

**Intel VMD.** With VMD on (the RAID or "Intel RST" storage setting of
many laptops' firmware), the NVMe disk sits behind the VMD controller and
is not on the PCI bus, and NovaOS has no VMD driver. The boot log then
says `[NVME] Intel VMD at 00:0e.0 (8086:a77f) hides the NVMe disks behind
it: ...`, `install` prints `Intel VMD (RST) hides the NVMe disks: turn it
off in the firmware setup.`, and turning VMD off shows the disk. (A
Windows installed with VMD on may need its storage driver changed before
it starts with VMD off; installing NovaOS replaces it anyway.)

**The firmware's boot entry.** As Windows Setup adds "Windows Boot
Manager", Setup adds a `Boot####` variable named "NovaOS" for
`\EFI\BOOT\BOOTX64.EFI` on NovaOS's EFI System Partition and puts it
first in `BootOrder`, keeping every other entry after it, through the
firmware's runtime services (`SetVariable`, `kernel/hal/efi.c`). The entry
names the partition by its GPT entry, place and unique GUID (the short
form firmware expands itself, as `bcdedit` and `efibootmgr` write it). The
log says `[SETUP] Added the firmware boot entry Boot0003 "NovaOS" for
nvme0n1, first in BootOrder (then "Windows Boot Manager")`, and every
start from the disk after that `[SETUP] The firmware has a "NovaOS" boot
entry for this disk`. If the firmware refuses the variable (some lock
`BootOrder` in their setup), the boot loader adds the entry on NovaOS's
first start from the disk, once it is started from the firmware's boot
menu (F12 on a ThinkPad); the log then says `[SETUP] Added the firmware
boot entry "NovaOS" for this disk`. After a whole-disk install an old
"Windows Boot Manager" entry stays in the list, pointing at a partition
that is gone; firmware skips it.

**The boot menu.** When another system's boot manager is on the PC
(Windows' `\EFI\Microsoft\Boot\bootmgfw.efi` on its ESP), NovaOS's boot
loader shows a menu with NovaOS and "Windows Boot Manager" for five
seconds before NovaOS starts; Up and Down or the line's number choose,
Enter starts ([design/installer.md](design/installer.md)).

## The embedded controller

A laptop's firmware describes its lid (`_LID`), battery (`_BIF`, `_BST`)
and charger (`_PSR`) in AML that reads fields of an `EmbeddedControl`
operation region: registers of the embedded controller, a small
microcontroller on two I/O ports (0x62 and 0x66 on most PCs). uACPI
interprets the AML but has no controller of its own; `kernel/hal/ec.c` is
its address space handler for that region. It finds the controller in
the ECDT table or as the PNP0C09 device (whose `_CRS` gives the ports),
reads and writes each byte with the RD_EC and WR_EC commands, takes the
ACPI global lock when `_GLK` asks, and lets the controller's AML know it
can use the region (`_REG`) before any `_INI` method runs.

The controller raises an event (its GPE, from `_GPE` or the ECDT) when the
lid moves, the charger is plugged in or the battery changes. NovaOS asks
it which one (QR_EC) and runs the firmware's `_Qxx` method for it, which
usually notifies the lid or the battery. The events are also polled once
a second, in case an edge goes missing. The log shows the controller
(`[EC] \_SB_.PCI0.LPCB.EC__: ports 0x62/0x66 (from the ECDT), GPE 0x...`)
and each kind of event the first time it comes (`[EC] Event 0x2a: _Q2A`).

## Passive cooling, GPE blocks and PCI bridges

**Throttling.** A thermal zone at or above its passive trip point
(`_PSV`) slows the processors in its `_PSL` (all of them if it has
none). Every `_TSP` (a second if not given) it changes their performance
by `-(_TC1 × (T now − T before) + _TC2 × (T now − _PSV))` percent, the
formula of ACPI 6.5 section 11.1.5 (4 and 3 when the firmware gives no
constants), until the zone has cooled and the processors are back to
full speed. `kernel/hal/throttle.c` puts each processor in the
shallowest T-state at or below what the hottest zone asks for, never
shallower than `_TPC` allows (Notify 0x82 when that changes):

- with `_PTC` and `_TSS`, it writes the state's control value to the
  control register and checks the status register, in I/O or memory
  space; a register in FFixedHW is Intel's `IA32_CLOCK_MODULATION` MSR,
  which each CPU writes for itself on its next timer tick;
- without them, `P_CNT` in the processor's `P_BLK`, with the FADT's duty
  cycle field.

The log lists each throttled processor (`[ACPI] Processor
\_SB_.CPUS.C000 (CPU 0): 4 T-states (100% 75% 50% 25%), through _PTC,
...`) and each change (`[ACPI] \_SB_.CPUS.C000: T3 (25% performance) for
passive cooling`). `CallNtPowerInformation(ProcessorInformation)` reports
the lower `MhzLimit` while a CPU is throttled, and `ThermalInformation`
the zone's constants and processors. Many recent Intel laptops leave
T-states out of their tables and cool with the processor's own
mechanisms; there the log has no `Processor` line and nothing is
throttled.

**GPE blocks.** uACPI handles the FADT's GPE0 and GPE1 blocks (`\_GPE`)
itself. NovaOS also sets up each GPE block device (`ACPI0006`) from its
`_CRS`: the block's status and enable registers, in I/O or memory space,
and its interrupt, or the SCI when it names none. The `_Lxx` and `_Exx`
methods under the device run as `\_GPE`'s do, and a `_PRW` that names
the block (`Package () { Package () { \_SB.GPE1, 1 }, 3 }`) makes that
GPE a wake event. The log says `[ACPI] GPE block \_SB_.GPE1: 8 events at
I/O 0xea, on the SCI, IRQ 9`.

**Bridges.** A PCI function behind a PCI-to-PCI bridge (a PCIe root or
switch port, for one) has its interrupt from the bridge's own `_PRT`
when the firmware gives one, and otherwise through the standard swizzle:
pin `(pin + device) mod 4` on the bridge's device number, one bridge up
at a time, to a bus that has a `_PRT`. Its Interrupt Line register and
the interrupt a Windows driver gets both use that GSI. The log names
each bus behind a bridge and each function routed by the swizzle
(`[ACPI] PCI 01:00.0 INTA: GSI 22 (swizzled through its bridges)`).

**Tests.** The core boot's `powertest` heats the thermal zone in
`tests/acpi/lid-thermal.asl` past its passive trip point: CPU 0 goes to
its deepest T-state (`MhzLimit` 25%) and back once cool, the T-states
being pc-testdev's scratch memory. The same table's lid raises its event
and its wake through a GPE block device. The devices suite's `edu` boot
puts QEMU's edu device behind a PCIe root port, which QEMU describes
with no `_PRT`, and its driver's interrupts arrive.

## Sleep without S3

Laptops sold for Windows' Modern Standby, the T14 Gen 4 among them, often
have no S3 (`\_S3`) at all. On those, when the FADT has the low-power S0
idle flag or the firmware has an LPS0 device (`INT33A1` or `PNP0D80`),
Sleep (on the Start menu, or closing the lid) is low-power S0 idle:

1. The screen goes black.
2. The LPS0 device's `_DSM` is called the way Linux calls it, for each
   function the firmware has: Intel's function 3 (screen off), Microsoft's
   3 and 7, then Intel's 5 (entry), so the platform can lower its idle
   power.
3. The desktop waits while the CPUs halt between interrupts. Opening the
   lid or pressing the power button wakes it; so does a key or the mouse
   if the lid was open.
4. The exit calls (Intel's 6, Microsoft's 8 and 4, Intel's 4), and the
   desktop is drawn again.

Devices stay powered and programs keep running, so this saves less than
S3 would: what it turns off is what the platform does with the LPS0
calls, and the screen's picture, not the backlight (that needs a graphics
driver). `GetPwrCapabilities` reports `AoAc` (Modern Standby) there
instead of S3. On firmware that has S3, sleep is S3.

## Timers without an HPET

NovaOS calibrates the TSC and the local APIC timer against the HPET. Some
laptop firmware hides the HPET, and recent Intel chipsets may gate the
8254 PIT's clock as well, so measuring against the PIT could wait
forever. Without an HPET, NovaOS takes both clocks, in this order:

1. from Hyper-V's frequency MSRs, under Hyper-V;
2. from CPUID leaf 0x15 (the core crystal's frequency and the TSC's ratio
   to it), or leaf 0x16's base frequency where the crystal is not given,
   as Linux does;
3. by measuring against the PIT, giving up after a few seconds.

The log says which: `[APIC] Timer: ... from CPUID 0x15 (no HPET)`. The
`laptop` test accepts the CPUID or the PIT path, whichever the CPU model
QEMU offers leads to; QEMU's usual CPU models do not report leaf 0x15, so
the CPUID path has had little testing.

## The QEMU test

The devices suite's `laptop` boot (`tests/selftest/devices/laptop`)
starts QEMU without `\_S3` or an HPET, with `tests/acpi/laptop.asl`: an
embedded controller holding the lid, a battery and the charger, an LPS0
device, and a thermal zone that passes the lid position the test sets to
the controller. QEMU emulates no embedded controller, so for the one with
`_HID` `NOVA0EC1` NovaOS serves a model of one in `ec.c` (the real command
and status protocol, with an event the AML can raise). The boot has the
ISO on a USB stick and an empty NVMe disk:

- `battery` reads 60% and 4 hours through the controller;
- the lid closes, NovaOS sleeps in S0 idle (with the LPS0 calls), and
  opening it wakes the machine;
- `install nvme0n1` installs NovaOS on the NVMe disk, and after a restart
  the machine starts from that disk and adds its boot entry.

```bash
python3 tools/selftest.py --suite devices --only 'ec battery,lid sleep (S0 idle),install on nvme,start from nvme'
```

## Checks on the T14

None of these has been done yet. Start from the USB stick (written as
[building.md](building.md) says), with Secure Boot off; F12 opens the
boot menu. Running live from the stick, the log goes to
`\EFI\NOVA\bootlog.txt` on it, which another computer can read; on the
installed system the Terminal's `dmesg` shows it.

1. **The disk.** In the Terminal, `devices` lists an NVMe controller with
   the driver `NVMe`, and `install` lists the disk (`nvme0n1`). If it says
   Intel VMD hides the disks, turn VMD (Intel RST) off in the firmware
   setup (F1) and start again.
2. **The embedded controller.** `dmesg` has an `[EC]` line with the ports
   (0x62/0x66) and a GPE, and `[ACPI] Battery`, `[ACPI] AC adapter` and
   `[ACPI] Lid` lines. `battery` prints the charge and "Power source: AC";
   unplug the charger and run it again within a few seconds: "Power
   source: battery" and a time left.
3. **Timers.** `dmesg` has the `[APIC] Timer:` line (the HPET, or CPUID
   0x15), and `sleeptest timer` passes.
4. **Sleep.** Close the lid for ten seconds, then open it: the screen was
   dark and the desktop comes back. `dmesg` shows `[SHELL] Lid closed:
   sleeping`, `[SLEEP] No S3 on this machine: sleeping in low-power S0
   idle`, the `[ACPI] LPS0` lines and `[SLEEP] Woke up after N s (the lid
   opened)`. (Firmware with S3 shows `[SLEEP] Woke up after N s` from S3
   instead.)
5. **Install.** `install nvme0n1` (this erases the disk, and Windows with
   it), then shut down, take the stick out and switch on: NovaOS starts
   from the disk, and `dmesg` shows `[SETUP] Added the firmware boot
   entry "NovaOS" for this disk`. If the firmware starts something else,
   choose the disk in the F12 menu once.
6. **Sleep again, installed.** Repeat check 4 on the installed system.

If a check fails, the `[EC]`, `[ACPI]`, `[SLEEP]`, `[APIC]` and `[SETUP]`
lines of the log (or `bootlog.txt` from the stick) say how far it got.
The audio, touchpad and microphone checks are in
[hardware.md](hardware.md#checking-audio-and-the-touchpad-on-the-t14-step-214),
and the Ethernet check in [ethernet.md](ethernet.md#testing-it).
