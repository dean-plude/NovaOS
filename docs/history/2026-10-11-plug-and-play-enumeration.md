## Plug and Play: setupapi and cfgmgr32 enumerate the device tree

Windows programs can now walk NovaOS's devices the way they do on
Windows.  cfgmgr32 serves the kernel's Plug and Play tree:
`CM_Locate_DevNode` (devices no longer present only with
`CM_LOCATE_DEVNODE_PHANTOM`), `CM_Get_Child`, `CM_Get_Sibling`,
`CM_Get_Parent` and `CM_Get_Depth`, `CM_Get_DevNode_Status` with the
`DN_*` flags and problem codes, device ID lists filtered by enumerator,
service, setup class, bus relations and presence, registry properties
(`CM_DRP_*`) and `DEVPKEY_*` properties, a device's hardware and
software keys (`CM_Open_DevNode_Key`), and interface lists for every
interface class the tree exposes (USB devices and hubs, keyboards, mice,
disks, audio, network and display adapters).  The game controllers stay
the HID devices hid.dll opens, now children of their USB devices.
setupapi's device information sets are built from the same tree: by
setup class or all classes, by enumerator (`USB`, or one instance ID),
by interface class, present or not, and `SetupDiOpenDeviceInfo`,
`SetupDiOpenDeviceInterface` and `SetupDiOpenDevRegKey` work by name.
`pnputil /enum-devices` lists the devices (`/connected`,
`/disconnected`, `/class`, `/instanceid`, `/deviceid`, `/ids`) and with
`/drivers` the published driver packages whose models match each one,
shown outranked by the NovaOS driver that runs it, since NovaOS loads no
kernel-mode driver.  `devenumtest` and three `pnputil` checks in the
devices suite's "pnp" boot cover it with a USB keyboard plugged in.
