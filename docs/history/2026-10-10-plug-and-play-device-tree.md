## Plug and Play: the device tree

NovaOS keeps a Plug and Play device tree the way Windows' PnP manager
does (`kernel/io/pnp.c`): the root (`HTREE\ROOT\0`), the PCI bus and
every PCI function found at boot, each USB controller's root hub, and
the USB devices plugged in at boot or later, with a node per function of
a composite device and a `HID\...` node (class Keyboard or Mouse, or a
game controller) or a `USBSTOR\Disk...` node (class DiskDrive) under the
function that is one.  Each node has a Windows device instance ID,
hardware and compatible IDs, a setup class, a description and the NovaOS
driver that runs it; a device nothing drives has problem 28, as on
Windows without a driver.  A device that is unplugged keeps its node,
marked not present, and gets it back when it returns to the same port.
Every arrival and removal is an event with a sequence number, which
programs read and wait for through `NtNovaGuiCtl`'s `CTL_PNP`
(`novapnp.h`); the kernel log shows them as `[PNP] arrival:` and
`[PNP] removal:` lines.  The devices suite's new "pnp" boot plugs a USB
keyboard, a hub and a USB stick behind it into an xHCI controller through
QEMU's monitor and takes them out again while `pnptest` follows the tree.
The PlugPlay service, `WM_DEVICECHANGE` and setupapi's and cfgmgr32's
enumeration of these nodes come next.
