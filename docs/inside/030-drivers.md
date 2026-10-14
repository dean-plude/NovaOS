- **Built-in drivers** (`kernel/drivers/`, `kernel/hal/`, `kernel/fs/`):
  NovaOS's own drivers, compiled into the kernel; they play the part of
  Windows' inbox drivers ([drivers.md](drivers.md), [hardware.md](hardware.md)).
  - **Storage**: AHCI SATA and NVMe disks (NovaOS installs to and boots
    from either); MBR and GPT partition tables, read and written; every partition a
    volume with a `\\?\Volume{GUID}\` name, as on Windows, its drive
    letter kept in `HKLM\SYSTEM\MountedDevices`; FAT16/FAT32 (writable on
    fixed disks); NTFS with read, write and format (drive C: with file
    ACLs and hard links, and other drives as D:, E:, ...), repaired by
    NovaOS itself after a power cut.
  - **Network**: Intel e1000/e1000e (82540EM, 82574L and the I219 built
    into Intel PCs) and virtio-net.
  - **Sound**: Intel High Definition Audio, playback and recording, with
    headphone detection; a laptop's digital microphones through Sound Open
    Firmware on Intel's audio DSP; a kernel mixer.
  - **USB**: xHCI, EHCI, OHCI and UHCI controllers, any number of each,
    with hubs; HID keyboards, mice, tablets, pens (pressure, tilt, barrel
    rotation and buttons, eraser) and multi-touch screens; mass storage
    (FAT and NTFS sticks, hot-plugged as the next drive letter); USB Audio
    Class 1 and 2 speakers, headsets and microphones; wired Xbox 360 and
    Xbox One controllers and HID game pads (for XInput, DirectInput 8, Raw
    Input and `hid.dll`).
  - **Other input**: PS/2 keyboards and mice; I2C-HID touchpads on Intel's
    LPSS I2C controllers, found through ACPI and read on their GPIO
    interrupt (tap to click, two-finger right click and scrolling);
    virtio multi-touch screens, pens and tablets.
  - **Display**: a VBE "DISPI" driver for QEMU's standard VGA, QXL,
    virtio-vga, VMware and bochs-display adapters and VirtualBox's VBoxVGA
    (modes switched at run time, page flipping, more adapters as more
    monitors); a Cirrus GD5446 driver; a virtio-gpu driver
    (see Graphics below); the UEFI framebuffer as the fallback.
  - **Platform**: ACPI through uACPI's AML interpreter (power-off, reset,
    power and sleep buttons, S3 sleep or low-power S0 idle, batteries and
    AC adapters, the lid, thermal zones, wake devices, PCI interrupt
    routing, the embedded controller), the I/O APIC, the HPET and the CMOS
    clock.
  - **Plug and Play**: a device tree of the PCI and USB devices with
    Windows instance, hardware and compatible IDs, setup classes and
    drivers, arrivals and removals as events, walked with cfgmgr32,
    setupapi and `pnputil /enum-devices`.
