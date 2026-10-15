- Plug and Play: a device tree the Windows way.  Devices plugged in or
  removed while NovaOS runs already work (USB hot-plug: keyboards, mice,
  hubs, sticks, audio, game controllers), and ~~a PnP device tree~~
  Done: the root, the PCI functions, the USB root hubs and devices, their
  functions and HID and disk children, with Windows instance, hardware
  and compatible IDs, classes, drivers, devices not present kept, and
  arrival and removal events for programs (`CTL_PNP`).  ~~The PlugPlay
  service, `WM_DEVICECHANGE`~~ Done: the PlugPlay service running and
  keeping `HKLM\SYSTEM\CurrentControlSet\Enum`,
  `RegisterDeviceNotification` (`DBT_DEVICEARRIVAL`,
  `DBT_DEVICEQUERYREMOVE`, `DBT_DEVICEQUERYREMOVEFAILED`,
  `DBT_DEVICEREMOVECOMPLETE`, `DBT_DEVNODES_CHANGED`, volumes, services and
  open device handles) and `CM_Register_Notification`.  ~~setupapi's and cfgmgr32's
  enumeration of the tree~~ Done: `SetupDiGetClassDevs` for every class,
  enumerator and interface class, `CM_Locate_DevNode`,
  `CM_Get_Child`/`Sibling`/`Parent`, device ID lists, status, registry
  and `DEVPKEY` properties (a device's bus type and container too), a
  device interface's properties (`CM_Get_Device_Interface_PropertyW`),
  device keys, and `pnputil /enum-devices`
  matching the driver store.
