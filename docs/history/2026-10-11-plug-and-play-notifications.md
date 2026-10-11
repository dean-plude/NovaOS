## Plug and Play: the PlugPlay service and WM_DEVICECHANGE

Windows programs now hear of devices arriving and leaving the way they
do on Windows.  `RegisterDeviceNotification` (wide and ANSI) delivers
`WM_DEVICECHANGE` with `DBT_DEVICEARRIVAL` and `DBT_DEVICEREMOVECOMPLETE`
and a `DEV_BROADCAST_DEVICEINTERFACE` (the interface's `\\?\USB#VID_...`
symbolic link) to a window registered for an interface class, or for all
of them, message-only windows included, and as
`SERVICE_CONTROL_DEVICEEVENT` to a service registered with its status
handle; every top-level window, hidden ones too, gets a USB stick's drive
letter as a `DEV_BROADCAST_VOLUME` and `DBT_DEVNODES_CHANGED`.
`CM_Register_Notification` calls back for interface classes and device
instances (enumerated, started, removed).  A thread of the program
follows the kernel's device events once it registers, or once the
desktop tells one of its windows that devices changed.  The new PlugPlay
service (`plugplay.exe`, started automatically) is reported running and
keeps `HKLM\SYSTEM\CurrentControlSet\Enum` current, a key per device with
its description, hardware and compatible IDs, class, class GUID, driver
and location.  `devnotifytest` in the devices suite's "pnp" boot checks
all of it as a USB keyboard and a FAT USB stick are plugged in and taken
out.
