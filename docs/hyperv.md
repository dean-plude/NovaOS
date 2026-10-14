# NovaOS in Hyper-V

**Status: untested.** NovaOS has drivers for Hyper-V's synthetic keyboard
and mouse, for its SCSI disks and synthetic video, and for its shutdown,
heartbeat and time sync integration services, but it has never been run on a real Hyper-V host. The drivers
were written from Linux's and FreeBSD's, and their protocol code is
tested on the build machine without a VM ([below](#tests)). Checking them
in a Hyper-V VM is roadmap step 23.4. Reports are welcome as issues, with
the serial log described below.

NovaOS starts only from UEFI, so in Hyper-V it needs a **Generation 2**
virtual machine (Generation 1 has BIOS firmware only). A Generation 2 VM
has no PS/2 controller, no USB controller and no emulated PCI devices:
its keyboard, mouse, disks, network adapter and video are *synthetic
devices* on **VMBus**, Hyper-V's own bus. NovaOS drives the keyboard, the
mouse, the SCSI disks ([below](#disks)), the video ([below](#display))
and the integration services ([below](#integration-services)); the rest is listed under
[What does not work yet](#what-does-not-work-yet). Full Gen 2 support is
a roadmap item ([ROADMAP.md](ROADMAP.md)), one device per step: network (netvsc), Hyper-V's own clock and timers, key-value exchange.

## Making the VM

In Hyper-V Manager: New, Virtual Machine, and

1. **Generation 2**.
2. At least 4096 MB of startup memory. Turning off dynamic memory is
   simplest (NovaOS has no balloon driver).
3. No network connection (NovaOS has no driver for Hyper-V's network
   adapter).
4. A virtual hard disk (the wizard's new VHDX is fine) to install NovaOS
   on, or "Attach a virtual hard disk later" to run it live only.
5. "Install an operating system from a bootable image file": `nova.iso`.

Then, in the VM's Settings, before starting it:

- **Security**: untick **Enable Secure Boot** (NovaOS's boot loader is
  not signed).
- **Processor**: any number of virtual processors.

Or in PowerShell, as administrator:

```powershell
New-VM -Name NovaOS -Generation 2 -MemoryStartupBytes 4GB -NoVHD
Set-VMFirmware -VMName NovaOS -EnableSecureBoot Off
Set-VMMemory -VMName NovaOS -DynamicMemoryEnabled $false
Add-VMDvdDrive -VMName NovaOS -Path C:\path\to\nova.iso
Set-VMFirmware -VMName NovaOS -FirstBootDevice (Get-VMDvdDrive -VMName NovaOS)
Start-VM NovaOS
```

Use **Basic session** in the VM window (View, untick Enhanced session).
An enhanced session is a Remote Desktop connection, which needs a service
inside the guest that NovaOS does not have.

## The keyboard and mouse

`kernel/drivers/vmbus.c` sets up VMBus:

1. CPUID leaf 0x40000000 says "Microsoft Hv", and leaf 0x40000003 lists
   the synthetic interrupt controller (SynIC) and the hypercalls VMBus
   needs.
2. NovaOS names its hypercall page and sets up the SynIC on CPU 0.
3. It connects to VMBus, offering protocol 5.3 first and then older ones
   down to 3.0, until the host takes one. If the host does not answer,
   NovaOS drops any connection left over from the firmware (UNLOAD) and
   tries once more.
4. It asks for the device list and opens the devices it has drivers for.
   Each gets two rings in shared memory, described to the host as a
   GPADL.

`kernel/drivers/hv_input.c` speaks the two devices' protocols:

- the **synthetic keyboard** sends set-1 scan codes, posted as the PS/2
  keyboard's keys are;
- the **synthetic mouse** is HID over VMBus: its report descriptor and
  input reports go through the HID decoding USB mice use, as an absolute
  pointer (the pointer follows the host's cursor, with no capture and no
  Ctrl+Alt+Left to release it).

The device poll thread reads both rings every 10 ms timer tick.

## Disks

`kernel/drivers/storvsc.c` drives Hyper-V's synthetic SCSI controller,
where a Generation 2 VM's virtual hard disks and DVD drive are.  It
agrees on a protocol version with the host (6.2, 6.0 or 5.1), then asks
each location of the controller what is there: every virtual hard disk
with 512-byte sectors (the VHDX default) becomes a disk named `hv0`,
`hv1`, ... in the order of the controller's locations.  The DVD drive is
left alone: NovaOS has already started from it.

So in a Hyper-V VM NovaOS can be installed like on a PC: the Setup app
(Install NovaOS on the desktop) or the Terminal's `install hv0` erases
the virtual hard disk and installs NovaOS on it
([install-and-power.md](install-and-power.md)).  Then remove the ISO
from the DVD drive (or move the hard disk first in the VM's Firmware
boot order) and restart: NovaOS starts from the virtual hard disk, and
drive C: is kept there.  A blank virtual hard disk attached to the live
system holds drive C: too, as a blank SATA disk does.

A VHDX made with 4 KiB logical sectors (`New-VHD -LogicalSectorSizeBytes
4096`) is not used (NovaOS's disk layer works in 512-byte sectors), nor
are disks added or removed while NovaOS runs (they are seen at the next
start).  Disks on a VM's second, third or fourth SCSI controller work
too.

## Display

`kernel/drivers/hv_video.c` drives Hyper-V's synthetic video device, so
the resolution can be changed in Settings > Display like on a PC.  NovaOS
starts on the firmware's frame buffer, then the driver agrees on a
protocol version with the host (3.5, as Windows 10 speaks it, else 3.2 or
3.0), tells the host that the frame buffer is the video memory, and lists
the modes that fit in it: with 3.5 the ones the host offers, otherwise
the common ones from 640x480 up.  A mode larger than the video memory the
host gives the VM (its size is in the log line below) is not listed; to
allow a larger screen, raise the VM's maximum resolution from PowerShell
with the VM off:

```powershell
Set-VMVideo -VMName NovaOS -HorizontalResolution 2560 -VerticalResolution 1600 -ResolutionType Maximum
```

Hyper-V shows the video memory only where the guest says it changed, so
the driver reports the whole screen 20 times a second while the host
asks for it.  NovaOS draws its own pointer (the host's is hidden).

## Integration services

`kernel/drivers/hv_util.c` answers three of the integration services
Hyper-V Manager lists under the VM's Settings (all on by default):

- **Operating system shutdown**: the host's Shut Down (Hyper-V Manager,
  `Stop-VM`) and Restart (`Restart-VM`) shut NovaOS down or restart it as
  the Start menu does, saving drive C: first.  NovaOS refuses Hibernate.
- **Heartbeat**: Hyper-V Manager's Summary tab shows "Heartbeat: OK"
  while NovaOS answers.
- **Time synchronization**: at start, after a resume and whenever the
  clock drifts more than a second from the host's, the system time
  Windows programs read (`GetSystemTime`, `NtQuerySystemTime`) is set
  from the host's clock.  The taskbar clock still reads the VM's CMOS
  clock, which Hyper-V keeps from the host's too.

Each service first agrees on a version with the host: the newest of
framework 3.0 or 1.0, and shutdown 3.1, 3.0 or 1.0, time sync 4.0
(with the partition reference counter, so the time the message spent on
the way is added), 3.0 or 1.0, heartbeat 3.0 or 1.0.

## The clock

Hyper-V has no HPET, so NovaOS takes the TSC's and the local APIC
timer's frequencies from the MSRs Hyper-V offers for them, as Linux
does. The log says `[APIC] Timer: ... from Hyper-V's frequency MSRs (no
HPET)`.

## What the log says

NovaOS writes its log to COM1. To read it from a Generation 2 VM, give the
VM a named pipe for COM1 (with the VM off):

```powershell
Set-VMComPort -VMName NovaOS -Number 1 -Path \\.\pipe\novaos
```

and open `\\.\pipe\novaos` with PuTTY (Connection type "Serial", the pipe
as the serial line) after starting the VM. Once NovaOS is up, the
Terminal's `dmesg` shows the same log.

A working start should show lines like these (the device numbers, the
version and the descriptor's size depend on the host):

```
[VMBUS] Hyper-V found (features 0x..., privileges 0x...)
[VMBUS] Connected: VMBus 5.3 (CPU 0 is virtual processor 0)
[VMBUS] Device 1: keyboard {f912ad6d-2b17-48ea-bd65-f927a61c7684}
[VMBUS] Device 2: mouse {cfa8b69e-5b4a-4cc0-b98b-8ba1a1f3f95a}
...
[VMBUS] Hyper-V keyboard: channel 1 open
[VMBUS] Hyper-V mouse: channel 2 open
[HVINPUT] keyboard: protocol 1.0 accepted
[HVINPUT] mouse: protocol 2.0 approved
[HVINPUT] mouse: 045e:0621, 67-byte report descriptor: absolute pointer
[VMBUS] Hyper-V shutdown: channel 3 open
[HVUTIL] shutdown: framework 3.0, messages 3.1
[HVUTIL] time sync: framework 3.0, messages 4.0
[HVUTIL] time sync: clock set from the host's (moved +0.012 s)
[HVUTIL] heartbeat: framework 3.0, messages 3.0
[VMBUS] Hyper-V SCSI: channel 6 open
[STORVSC] controller 0: protocol 6.2, transfers up to 128 KiB
[STORVSC] controller 0, location 0: hv0 "Msft     Virtual Disk", 130048 MiB
[STORVSC] controller 0, location 1: Msft     Virtual DVD-ROM (DVD drive; NovaOS started from it already): not used
[VMBUS] Hyper-V video: channel 7 open
[HVVIDEO] Synthetic video 3.5: 8 MiB of video memory at 0xf8000000, 9 mode(s)
[DISPLAY] Hyper-V synthetic video: 1024x768, 9 mode(s)
```

Every device the host offers is listed, with a name for the common ones
(network, key-value exchange and dynamic memory are named and marked
"no driver"). Other
lines say what went wrong:

| Line | Meaning |
|---|---|
| no `[VMBUS]` line at all | NovaOS did not see Hyper-V in CPUID: not a Hyper-V VM |
| `Hyper-V without the SynIC or its hypercalls ...: no VMBus` | the hypervisor does not offer what VMBus needs |
| `The hypervisor did not enable hypercalls` | the hypercall page was refused |
| `VMBus 5.3: not taken` | the host refused that version; NovaOS tries the next one |
| `VMBus 5.3: no answer` | the host did not answer; NovaOS drops any older connection (`Unload: done` or `Unload: no answer`) and tries once more |
| `The host took no VMBus version NovaOS speaks` | every version was refused |
| `N devices offered` | the host's device list ended (`(the list never ended)` if it timed out) |
| `... the host did not take its rings` / `... the host did not open it` | a channel failed to open, with the host's status |
| `No channel open` | no device NovaOS drives was offered, or none opened |
| `[HVINPUT] ... refused` | the device refused the protocol version NovaOS asked for |
| `[HVUTIL] ...: no version in common with the host` | that integration service stays off |
| `[HVUTIL] shutdown: the host asks to shut down` | Shut Down from the host; `[SHELL] Shutting down` follows |
| `[STORVSC] controller N: no answer to BEGIN_INITIALIZATION`, `... the host took no protocol version` | the SCSI controller did not come up: no disks |
| `[STORVSC] controller N: no disks` | the controller has no virtual hard disk attached |
| `... its blocks are not 512 bytes` | a VHDX with 4 KiB logical sectors: make one with 512 |
| `[STORVSC] controller N: no answer in 30 s: given up` | a request got no answer; that controller's disks stop working |
| `[HVVIDEO] No answer to the version request`, `The host took no version NovaOS speaks`, `The host did not take the video memory` | the video device did not come up: the screen stays at the firmware's resolution |
| `[DISPLAY] 1920x1080 refused by Hyper-V` | the host did not take the mode change; the screen stays as it was |

## What does not work yet

| Device | State |
|---|---|
| Keyboard, mouse | Drivers exist (VMBus); not yet checked on a real host |
| Display | Driver exists ([above](#display)); not yet checked on a real host. One screen only |
| Disks | Driver exists ([above](#disks)); not yet checked on a real host. 4 KiB-sector VHDX files and disks added while running are not used |
| Network | No driver for Hyper-V's network adapter (netvsc) |
| Sound | Hyper-V has no virtual sound card (only through an enhanced session) |
| Shutdown, time sync, heartbeat | Drivers exist ([above](#integration-services)); not yet checked on a real host |
| Key-value exchange, guest services, VSS backup, dynamic memory | No drivers: Hyper-V Manager shows no addresses for the VM, and memory stays as it started |

## Tests

`tests/host/hv_input.c`, built and run by `tests/tools/test_hv_input.py`
(part of the host tool tests CI runs with `python3 -m unittest discover -s
tests/tools`), checks the ring buffer (wrapping, a full ring, the
signalling hints) and both input protocols on the build machine, without
a VM.  `tests/host/hv_util.c` (`tests/tools/test_hv_util.py`) does the same
for the integration services: the version each picks from the host's
lists (and none when nothing matches), answers carrying the host's
transaction ID, the heartbeat's sequence number, each shutdown flag, and
time sync's SYNC, SAMPLE and probe messages with and without version 4's
reference counter.  `tests/host/hv_storvsc.c` (`tests/tools/test_hv_storvsc.py`)
runs the SCSI driver against a fake host: the GPA-direct packet layout,
bring-up with a refused protocol version, the scan (a disk, the DVD drive,
a 4 KiB-sector disk, an 8 TiB disk that needs READ CAPACITY(16), empty
locations answered both ways Hyper-V does), a unit attention, reads and
writes split at the host's largest transfer, flush, and a host that stops
answering.  `tests/host/hv_video.c` (`tests/tools/test_hv_video.py`) runs
the video driver against a fake host: each message's size, the version
fallback from 3.5 to 3.0, the host's mode list filtered to what fits,
the video memory's location, mode changes, the dirty-rectangle timing and
the host turning it off, and a host that refuses or never answers. Its mouse case uses the device information layout Hyper-V sends (a
32-byte block: a 32-bit size, vendor, product, version and eleven
reserved 16-bit words, then the HID descriptor), checked against
FreeBSD's `hv_hid.c`.

QEMU cannot offer Hyper-V's VMBus devices, so the VMBus connection itself
(hypercalls, the SynIC, the handshake, opening channels) is not tested in
CI and can only be checked in a real Hyper-V VM.
